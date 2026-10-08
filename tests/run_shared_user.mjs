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
let proxy;
let child;
let temporary;
let ownTables = false;
let poll;
let stopping;
let controlFailure;
const stallSockets = new Set();
const proxySockets = new Set();
const proxyTimers = new Set();
let droppedCreate;
let repeatedCreate;
let wrongIdentity = false;
let wrongScope = false;
let missingPart = false;
let oldResponse = false;
let revisionConflict = false;

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
    // 代理只篡改明示测试故障，不直接调用Qt业务回调；正常请求仍由真实API/MySQL处理。
    const consume = async (name) => {
        const filename = path.join(temporary, name);
        if (!(await exists(filename))) return false;
        await rm(filename);
        return true;
    };
    proxy = http.createServer(async (request, response) => {
        try {
            const chunks = [];
            let size = 0;
            for await (const chunk of request) {
                size += chunk.length;
                if (size > 5 * 1024 * 1024) throw new Error('bounded proxy request');
                chunks.push(chunk);
            }
            const body = Buffer.concat(chunks);
            const route = request.url.split('?', 1)[0];
            const post = request.method === 'POST' && route === '/api/annotations';
            const update = request.method === 'PUT' && route.startsWith('/api/annotations/');
            const list = request.method === 'GET' && route === '/api/annotations';
            const detail = request.method === 'GET' && route === '/api/equipment' && request.url.includes('id=');
            const drop = post && await consume('proxy.drop-create.arm');
            const corruptIdentity = update && await consume('proxy.wrong-identity.arm');
            const corruptScope = list && await consume('proxy.wrong-scope.arm');
            const removePart = detail && await consume('proxy.missing-part.arm');
            const delay = list && await consume('proxy.delay-notes.arm');
            const outgoing = http.request({ hostname: '127.0.0.1', port: address.port,
                path: request.url, method: request.method, headers: { ...request.headers, host: `127.0.0.1:${address.port}` } });
            outgoing.setTimeout(15000, () => outgoing.destroy());
            response.once('close', () => { if (!response.writableEnded) outgoing.destroy(); });
            const upstream = await new Promise((resolve, reject) => {
                outgoing.once('response', resolve);
                outgoing.once('error', reject);
                outgoing.end(body);
            });
            const returned = [];
            let received = 0;
            for await (const chunk of upstream) {
                received += chunk.length;
                if (received > 6 * 1024 * 1024) throw new Error('bounded proxy response');
                returned.push(chunk);
            }
            let bytes = Buffer.concat(returned);
            const ok = upstream.statusCode >= 200 && upstream.statusCode < 300;
            if (update && upstream.statusCode === 409) {
                const rejected = JSON.parse(bytes.toString('utf8'));
                assert.equal(rejected.error.code, 'REVISION_CONFLICT', '两人旧版本必须真实CAS冲突');
                revisionConflict = true;
                await writeFile(path.join(temporary, 'proxy.cas-conflict'), '409-REVISION_CONFLICT');
            }
            if (drop && ok) {
                assert.equal(upstream.statusCode, 201, '首次新增必须实际提交成功后才丢响应');
                droppedCreate = JSON.parse(body.toString('utf8'));
                const result = JSON.parse(bytes.toString('utf8')).data;
                assert.equal(result.id, droppedCreate.requestId);
                await writeFile(path.join(temporary, 'proxy.create-dropped'), result.id);
                response.destroy();
                return;
            }
            if (post && droppedCreate) {
                const posted = JSON.parse(body.toString('utf8'));
                if (posted.requestId === droppedCreate.requestId) {
                    assert.deepEqual(posted, droppedCreate, '显式重试必须使用同UUID原payload');
                    assert.equal(upstream.statusCode, 200, '相同请求幂等重试返回200');
                    const result = JSON.parse(bytes.toString('utf8')).data;
                    assert.equal(result.id, droppedCreate.requestId);
                    assert.equal(result.revision, 2, '真实另一客户端已更新，再重试取得当前DTO');
                    repeatedCreate = posted.requestId;
                    await writeFile(path.join(temporary, 'proxy.idempotent-confirmed'), 'same-uuid-current-dto');
                }
            }
            if (ok && (corruptIdentity || corruptScope || removePart)) {
                const document = JSON.parse(bytes.toString('utf8'));
                if (corruptIdentity) {
                    document.data.id = '11111111-1111-4111-8111-111111111111';
                    document.data.partId = '..';
                    wrongIdentity = true;
                }
                if (corruptScope) {
                    assert.ok(document.data.length > 0, '需要真实批注供关联错误注入');
                    document.data[0].partId = '..';
                    wrongScope = true;
                }
                if (removePart) {
                    document.data.parts = document.data.parts.filter((item) => item.id !== '零件/支架 ');
                    missingPart = true;
                }
                bytes = Buffer.from(JSON.stringify(document));
            }
            const finish = () => {
                if (!response.destroyed && !response.writableEnded) {
                    response.writeHead(upstream.statusCode, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': bytes.length });
                    response.end(bytes);
                }
            };
            if (delay && ok) {
                await writeFile(path.join(temporary, 'proxy.delay-notes.received'), 'upstream-finished');
                const timer = setTimeout(() => {
                    proxyTimers.delete(timer);
                    finish();
                    oldResponse = true;
                    void writeFile(path.join(temporary, 'proxy.delay-notes.finished'), 'late-response').catch((error) => { controlFailure = error; });
                }, 500);
                proxyTimers.add(timer);
            } else finish();
        } catch (error) {
            if (error instanceof assert.AssertionError) controlFailure = error;
            if (!response.destroyed && !response.writableEnded) {
                response.writeHead(503, { 'Content-Type': 'application/json' });
                response.end(JSON.stringify({ error: { code: 'TEST_PROXY_UNAVAILABLE', message: '共享测试服务连接已断开。' } }));
            }
        }
    });
    proxy.on('connection', (socket) => {
        proxySockets.add(socket);
        socket.once('close', () => proxySockets.delete(socket));
    });
    await new Promise((resolve, reject) => {
        proxy.once('error', reject);
        proxy.listen(0, '127.0.0.1', () => { proxy.off('error', reject); resolve(); });
    });
    // 此轮服务由harness持有；文件握手避免Qt进程依赖Node内部回调。
    poll = setInterval(() => {
        if (stopping) return;
        stopping = (async () => {
            for (const command of ['pause', 'resume', 'stop']) {
                if (!(await exists(path.join(temporary, `${command}.request`)))
                    || await exists(path.join(temporary, `${command}.done`))) continue;
                if (command === 'resume') {
                    app = await createApp({ ...config, host: '127.0.0.1', port: address.port });
                    await app.start(address.port, '127.0.0.1');
                } else await app.close();
                await writeFile(path.join(temporary, `${command}.done`), command);
            }
            stopping = null;
        })().catch((error) => { controlFailure = error; });
    }, 20);
    const executable = path.resolve(process.argv[2] ?? path.join(root, 'build/bin/shared_user_tests.exe'));
    const platform = process.argv[3] ?? 'offscreen';
    assert.ok(['windows', 'offscreen'].includes(platform), '平台只允许windows/offscreen');
    const snapshotDir = path.resolve(process.argv[4] ?? path.join(root, `build/shared-user-0.5-${platform}`));
    child = spawn(executable, ['-platform', platform, '--snapshot-dir', snapshotDir], {
        cwd: root, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'],
        env: { ...process.env, EQUIPMENT_SERVICE_CONFIG: qtConfig, EQUIPMENT_TEST_CONTROL_DIR: temporary,
            EQUIPMENT_TEST_STALL_URL: `http://127.0.0.1:${stall.address().port}`,
            EQUIPMENT_TEST_PROXY_URL: `http://127.0.0.1:${proxy.address().port}` }
    });
    child.stdout.on('data', (data) => process.stdout.write(data));
    child.stderr.on('data', (data) => process.stderr.write(data));
    const code = await new Promise((resolve, reject) => {
        const timer = setTimeout(() => { child.kill(); reject(new Error('Qt模拟测试超过120秒，已停止本次子进程')); }, 120000);
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
    assert.ok(droppedCreate && repeatedCreate === droppedCreate.requestId, '需真实完成同UUID结果不确定重试');
    assert.ok(revisionConflict && wrongIdentity && wrongScope && missingPart && oldResponse, '真实CAS与受控身份/取消场景须执行');
    const [annotations] = await pool.execute('SELECT equipment_id,part_id,text,author,updated_by,revision FROM annotations');
    assert.equal(annotations.length, 3, '幂等重试不重复新增，只有三条批注');
    assert.ok(annotations.every((item) => item.equipment_id === '共享/工艺 '));
    const primary = annotations.find((item) => item.text === '乙的待提交意见\n需要保留，不静默覆盖甲。');
    assert.ok(primary && primary.revision === 3 && primary.author === '工艺甲' && primary.updated_by === '工艺乙'
        && primary.part_id === '零件/支架 ', '两人CAS的最终持久内容与身份必须准确');
    const equipmentNote = annotations.find((item) => item.part_id === null);
    assert.ok(equipmentNote && equipmentNote.revision === 1 && equipmentNote.text === '整台设备：装配前复核清单。');
    const guarded = annotations.find((item) => item.text === '保留身份保护草稿。');
    assert.ok(guarded && guarded.revision === 4 && guarded.part_id === '零件/支架 ', '响应身份篡改不改变真实提交内容');
    console.log('ANNOTATION_HARNESS accepted=1 casPersisted=1 sameUuid=1 currentDto=1 wrongIdentityAndScope=1 oldResponse=1 restarted=1');
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
        for (const timer of proxyTimers) clearTimeout(timer);
        if (proxy) {
            for (const socket of proxySockets) socket.destroy();
            if (proxy.listening) await new Promise((resolve) => proxy.close(resolve));
        }
        if (stall) {
            for (const socket of stallSockets) socket.destroy();
            if (stall.listening) await new Promise((resolve) => stall.close(resolve));
        }
        if (ownTables) {
            // 仅清理本轮从空ev_test创建的表；批注联合FK依赖零件，必须先移除。
            // 不drop数据库、不触ev_shared/MySQL80；未取得空库所有权时不清理。
            await pool.execute('DROP TABLE IF EXISTS annotations');
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
