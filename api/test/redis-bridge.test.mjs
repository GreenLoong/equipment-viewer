import test from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter, once } from 'node:events';
import { PassThrough, Writable } from 'node:stream';
import net from 'node:net';
import { createRedisBridge } from '../tools/redis-bridge.mjs';
import { runWslSetup, toWslPath, validateRedisConfig, redisPurpose, linuxDirectory } from '../tools/setup-redis.mjs';

const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const fixture = { purpose: redisPurpose, host: '127.0.0.1', port: 6380, version: '7.4.7', linuxDirectory,
    distribution: 'Ubuntu-24.04', password: '0'.repeat(64) };
function fakeChild({ echo = false, hang = false, stalledInput = false } = {}) {
    const child = new EventEmitter();
    child.stdin = stalledInput ? new Writable({ highWaterMark: 1024, write(_chunk, _encoding, _callback) {} }) : new PassThrough();
    child.stdout = new PassThrough();
    child.stderr = new PassThrough();
    child.kills = 0;
    child.unref = () => {};
    let closed = false;
    const finish = () => {
        if (closed) return;
        closed = true;
        child.stdout.end();
        child.stderr.end();
        setImmediate(() => child.emit('close', 0));
    };
    if (echo) child.stdin.on('data', (chunk) => child.stdout.write(chunk));
    if (!hang) child.stdin.on('finish', finish);
    child.kill = () => { child.kills += 1; child.stdin.destroy(); finish(); return true; };
    return child;
}
async function connect(bridge) {
    const socket = net.connect({ host: '127.0.0.1', port: bridge.server.address().port });
    socket.on('error', () => {});
    await once(socket, 'connect');
    return socket;
}

test('环境配置与WSL参数固定，setup秘密仅stdin，stdout/stderr不混入日志', async () => {
    assert.equal(validateRedisConfig(fixture), fixture);
    assert.equal(toWslPath('D:\\工程\\api\\tools\\redis-stdio.py'), '/mnt/d/工程/api/tools/redis-stdio.py');
    assert.throws(() => validateRedisConfig({ ...fixture, port: 6379 }));
    let input = '';
    const result = await runWslSetup(fixture, 'setup', { spawnProcess: (binary, args, options) => {
        assert.equal(binary, 'wsl.exe');
        assert.equal(args.includes(fixture.password), false);
        assert.equal(options.windowsHide, true);
        const child = fakeChild({ hang: true });
        child.stdin.on('data', (chunk) => { input += chunk; });
        child.stdin.on('finish', () => {
            child.stderr.write('diagnostic only');
            child.stdout.end(JSON.stringify({ state: 'ready', port: 6380 }));
            setImmediate(() => child.emit('close', 0));
        });
        return child;
    }, timeoutMs: 1000 });
    assert.equal(result.state, 'ready');
    assert.equal(JSON.parse(input).password, fixture.password);
});

test('TCP原始字节双向传递，stderr不混RESP；close清理全部受控连接/子进程', async () => {
    let child;
    const bridge = createRedisBridge(fixture, { spawnProcess: (_binary, args, options) => {
        assert.equal(args.includes(fixture.password), false);
        assert.equal(options.windowsHide, true);
        child = fakeChild({ echo: true });
        return child;
    }, childCloseTimeoutMs: 50 });
    await bridge.start(0);
    const socket = await connect(bridge);
    const chunks = [];
    socket.on('data', (chunk) => chunks.push(chunk));
    child.stderr.write('not RESP');
    const payload = Buffer.from('*1\r\n$4\r\nPING\r\n');
    socket.write(payload);
    for (let index = 0; index < 100 && Buffer.concat(chunks).length < payload.length; ++index) await wait(5);
    assert.deepEqual(Buffer.concat(chunks), payload);
    await bridge.close();
    assert.equal(bridge.diagnostics().activeConnections, 0);
    assert.equal(bridge.server.listening, false);
    assert.equal(socket.destroyed, true);
    await bridge.close();
});

test('客户端断开/空闲，挂起子进程有界终止；未知端口不替换', async () => {
    const children = [];
    const bridge = createRedisBridge(fixture, { spawnProcess: () => {
        const child = fakeChild({ hang: true }); children.push(child); return child;
    }, idleTimeoutMs: 30, childCloseTimeoutMs: 20, killGraceMs: 50 });
    await bridge.start(0);
    const first = await connect(bridge);
    first.destroy();
    const second = await connect(bridge);
    await wait(100);
    assert.equal(bridge.diagnostics().activeConnections, 0);
    assert.equal(children.length, 2);
    assert.ok(children.every((child) => child.kills === 1));
    assert.equal(second.destroyed, true);
    await bridge.close();
    const occupied = net.createServer();
    await new Promise((resolve) => occupied.listen(0, '127.0.0.1', resolve));
    const other = createRedisBridge(fixture);
    await assert.rejects(other.start(occupied.address().port), { code: 'EADDRINUSE' });
    assert.equal(occupied.listening, true);
    await other.close();
    await new Promise((resolve) => occupied.close(resolve));
});

test('连接数限制与stdin背压阻止无界积累，关闭后资源归零', async () => {
    let spawned = 0;
    let child;
    const bridge = createRedisBridge(fixture, { spawnProcess: () => { spawned += 1; child = fakeChild({ stalledInput: true, hang: true }); return child; },
        maximumConnections: 1, childCloseTimeoutMs: 20, killGraceMs: 50 });
    await bridge.start(0);
    const first = await connect(bridge);
    const second = await connect(bridge);
    await wait(20);
    assert.equal(spawned, 1);
    assert.equal(second.destroyed, true);
    first.write(Buffer.alloc(2 * 1024 * 1024, 1));
    await wait(30);
    assert.ok(child.stdin.writableLength <= 256 * 1024, 'pipe暂停源流，不积累完整2MiB');
    const started = Date.now();
    await bridge.close();
    assert.ok(Date.now() - started < 1000);
    assert.equal(bridge.diagnostics().activeConnections, 0);
    assert.equal(child.kills, 1);
});

test('kill未发真实close的child持续计预算，后续close不能误报成功', async (t) => {
    let child;
    let spawned = 0;
    const bridge = createRedisBridge(fixture, { spawnProcess: () => {
        spawned += 1;
        child = fakeChild({ hang: true });
        child.kill = () => { child.kills += 1; return true; }; // 故意不发close，模拟无法确认退出。
        return child;
    }, maximumConnections: 1, childCloseTimeoutMs: 20, killGraceMs: 20 });
    t.after(async () => { child?.emit('close', 0); await bridge.close(); });
    await bridge.start(0);
    const first = await connect(bridge);
    first.destroy();
    await wait(80);
    assert.equal(bridge.diagnostics().activeConnections, 1);
    assert.equal(bridge.diagnostics().unconfirmedChildren, 1);
    const rejected = await connect(bridge);
    await wait(20);
    assert.equal(rejected.destroyed, true);
    assert.equal(spawned, 1, '未确认child仍占最大子进程预算');
    await assert.rejects(bridge.close(), /unconfirmed/);
    await assert.rejects(bridge.close(), /unconfirmed/);
    assert.equal(bridge.diagnostics().activeConnections, 1);
    child.emit('close', 0);
    assert.equal(bridge.diagnostics().activeConnections, 0);
    assert.equal(bridge.diagnostics().unconfirmedChildren, 0);
    await bridge.close();
});
