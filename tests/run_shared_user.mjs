import assert from 'node:assert/strict';
import http from 'node:http';
import { spawn } from 'node:child_process';
import { mkdtemp, writeFile, access, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import mysql from '../api/node_modules/mysql2/promise.js';
import { createApp, loadConfig } from '../api/src/server.mjs';

// 仅驱动独立用户测试服务；禁止回落正常配置、正常共享库或原MySQL80。
const root = fileURLToPath(new URL('..', import.meta.url));
let pool;
let app;
let stall;
let child;
let temporary;
let ownTables = false;
let poll;
let stopping;
let controlFailure;
const stallSockets = new Set();

async function exists(filename) {
    try { await access(filename); return true; } catch { return false; }
}

try {
    assert.ok(process.env.EV_TEST_CONFIG_FILE, '必须明确设置EV_TEST_CONFIG_FILE');
    const config = await loadConfig(process.env.EV_TEST_CONFIG_FILE);
    assert.equal(config.mysql.database, 'ev_test', '测试数据库必须是ev_test');
    assert.equal(config.mysql.port, 3310, '只允许独立MySQL3310');
    assert.equal(config.mysql.user, 'ev_app', '只允许独立ev_app账号');
    assert.ok(['127.0.0.1', 'localhost', '::1'].includes(config.mysql.host), '只允许本机MySQL');
    pool = mysql.createPool({ ...config.mysql, charset: 'utf8mb4', multipleStatements: false });
    const [tables] = await pool.execute('SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE()');
    assert.equal(tables.length, 0, 'ev_test非空时拒绝执行或清理，保留现有表');
    ownTables = true;
    app = await createApp({ ...config, host: '127.0.0.1', port: 0 });
    const address = await app.start(0, '127.0.0.1');
    temporary = await mkdtemp(path.join(tmpdir(), 'equipment-shared-user-'));
    const qtConfig = path.join(temporary, 'qt-service.json');
    await writeFile(qtConfig, JSON.stringify({ host: '127.0.0.1', port: address.port, token: config.token }), { mode: 0o600 });
    stall = http.createServer((request, response) => {
        // 真正接收Qt网络请求并保持pending；关闭时观察客户端socket，完全不访问数据库。
        request.resume();
        void writeFile(path.join(temporary, 'stall.received'), 'received').catch((error) => { controlFailure = error; });
        response.once('close', () => {
            void writeFile(path.join(temporary, 'stall.closed'), 'closed').catch((error) => { controlFailure = error; });
        });
    });
    stall.on('connection', (socket) => {
        stallSockets.add(socket);
        socket.once('close', () => stallSockets.delete(socket));
    });
    await new Promise((resolve, reject) => {
        stall.once('error', reject);
        stall.listen(0, '127.0.0.1', () => { stall.off('error', reject); resolve(); });
    });
    // 此轮服务由harness持有；文件握手避免Qt进程依赖Node内部回调。
    poll = setInterval(() => {
        if (stopping) return;
        stopping = (async () => {
            if (!(await exists(path.join(temporary, 'stop.request')))) { stopping = null; return; }
            await app.close();
            await writeFile(path.join(temporary, 'stop.done'), 'closed');
        })().catch((error) => { controlFailure = error; });
    }, 20);
    const executable = path.resolve(process.argv[2] ?? path.join(root, 'build/bin/shared_user_tests.exe'));
    const platform = process.argv[3] ?? 'offscreen';
    assert.ok(['windows', 'offscreen'].includes(platform), '平台只允许windows/offscreen');
    const snapshotDir = path.resolve(process.argv[4] ?? path.join(root, `build/shared-user-0.4-${platform}`));
    child = spawn(executable, ['-platform', platform, '--snapshot-dir', snapshotDir], {
        cwd: root, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'],
        env: { ...process.env, EQUIPMENT_SERVICE_CONFIG: qtConfig, EQUIPMENT_TEST_CONTROL_DIR: temporary,
            EQUIPMENT_TEST_STALL_URL: `http://127.0.0.1:${stall.address().port}` }
    });
    child.stdout.on('data', (data) => process.stdout.write(data));
    child.stderr.on('data', (data) => process.stderr.write(data));
    const code = await new Promise((resolve, reject) => {
        const timer = setTimeout(() => { child.kill(); reject(new Error('Qt模拟测试超过90秒，已停止本次子进程')); }, 90000);
        child.once('error', (error) => { clearTimeout(timer); reject(error); });
        child.once('exit', (value, signal) => { clearTimeout(timer); resolve(signal ? 1 : value); });
    });
    assert.equal(code, 0, 'Qt共享模拟用户验收失败');
    assert.ok(await exists(path.join(temporary, 'stop.done')), 'Qt必须真正触发本次服务关闭');
    assert.ok(await exists(path.join(temporary, 'stall.closed')), 'Qt关闭pending必须关闭真实连接');
    if (controlFailure) throw new Error('测试控制握手失败');
    const [equipment] = await pool.execute('SELECT id FROM equipment ORDER BY id');
    assert.deepEqual(equipment.map((item) => item.id), ['..', '共享/工艺 '], '只允许两项测试发布，保持精确身份');
    const [parts] = await pool.execute('SELECT COUNT(*) AS n FROM part');
    assert.equal(Number(parts[0].n), 4, '两次完整发布，重复操作未增改资料');
    console.log('SHARED_HARNESS accepted=1 mysql=ev_test port=3310 serviceStopped=1 pendingSocketClosed=1 simulated=1');
} catch (error) {
    // 不打印配置/凭证或SQL错误详情；保留断言的安全定位信息。
    console.error(error instanceof assert.AssertionError ? error.message : '共享用户测试未完成，请检查可执行文件、受控配置与上方控件日志。');
    process.exitCode = 1;
} finally {
    clearInterval(poll);
    if (child && child.exitCode === null) {
        child.kill();
        await new Promise((resolve) => {
            const timer = setTimeout(resolve, 2000);
            child.once('exit', () => { clearTimeout(timer); resolve(); });
        });
    }
    try {
        await stopping;
        await app?.close();
        if (stall) {
            for (const socket of stallSockets) socket.destroy();
            if (stall.listening) await new Promise((resolve) => stall.close(resolve));
        }
        if (ownTables) {
            // 仅清理本轮从空ev_test创建的三张表；不drop数据库、不触ev_shared/MySQL80。
            await pool.execute('DROP TABLE IF EXISTS part');
            await pool.execute('DROP TABLE IF EXISTS equipment');
            await pool.execute('DROP TABLE IF EXISTS schema_version');
            const [remaining] = await pool.execute('SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE()');
            assert.equal(remaining.length, 0, '本轮测试表清理后应为空');
            console.log('SHARED_HARNESS cleanup=1 ownedTestTablesRemoved=1');
        }
    } catch {
        console.error('本轮测试收尾未完成，保留日志并检查ev_test状态；不得清理正常数据库。');
        process.exitCode = 1;
    } finally {
        await pool?.end();
        if (temporary) {
            const resolved = path.resolve(temporary);
            assert.equal(path.dirname(resolved), path.resolve(tmpdir()), '只删除本次系统临时目录');
            assert.ok(path.basename(resolved).startsWith('equipment-shared-user-'), '临时目录前缀必须匹配');
            await rm(resolved, { recursive: true, force: true });
        }
    }
}
