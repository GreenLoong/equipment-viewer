import assert from 'node:assert/strict';
import http from 'node:http';
import { randomBytes, randomUUID } from 'node:crypto';
import { spawn } from 'node:child_process';
import { mkdtemp, readFile, writeFile, access, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import mysql from '../api/node_modules/mysql2/promise.js';
import { createClient } from '../api/node_modules/@redis/client/dist/index.js';
import { createApp, loadConfig } from '../api/src/server.mjs';
import { equipmentCacheKey } from '../api/src/cache.mjs';
import { createRedisBridge } from '../api/tools/redis-bridge.mjs';
import { loadRedisConfig, redisConfigFile } from '../api/tools/setup-redis.mjs';
import { runRestoreCli } from './verify_mysql_restore.mjs';

// 独立0.6验收；只拥有本次空ev_test/随机namespace/Windows桥，绝不关闭LinuxRedis或常规服务。
const root = fileURLToPath(new URL('..', import.meta.url));
const equipmentId = "缓存/设备 '工艺' ";
const partId = '零件/支架 ';
const initialNote = '初始缓存测试工艺意见。';
const latestNote = '他人已更新：采用最新工艺卡。';
const namespace = 'su6-' + randomBytes(12).toString('hex');
const noteId = randomUUID();
const pause = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
let app;
let pool;
let bridge;
let bridgeOwned = false;
let bridgePaused = false;
let redis;
let redisSecret;
let proxy;
let child;
let ownedTables = false;
let temporary;
let poll;
let controlTask;
let controlFailure;
let stage = 'preflight';
let cacheDown = false;
let cacheRestored = false;
let downBefore;
let peerBefore;
let peerCacheCounters;
let guiExitUnconfirmed = false;
const observations = [];
const proxySockets = new Set();
const timing = [];

function fixture(id = equipmentId) {
    return { equipmentId: id, equipmentName: '缓存模拟设备', parts: [
        { id: partId, name: '同名支架', material: "钢'材", shape: 'box', dimensions: [100, 80, 60], position: [0, 0, 0] },
        { id: '..', name: '同名支架', material: '铝', shape: 'cylinder', dimensions: [10, 80], position: [160, 0, 0] }
    ] };
}

async function exists(filename) {
    try { await access(filename); return true; } catch { return false; }
}

function cacheCounters(value) {
    return [value.cache.hits, value.cache.misses, value.cache.bypass, value.cache.sets, value.cache.invalidValues];
}

// 超时真正destroy此测试客户端；不以Promise.race留下活Redis连接/无界离线队列。
async function redisBounded(action, timeoutMs = 3000) {
    return new Promise((resolve, reject) => {
        let settled = false;
        const finish = (error, value) => {
            if (settled) return;
            settled = true;
            clearTimeout(timer);
            if (error) reject(error); else resolve(value);
        };
        const timer = setTimeout(() => {
            try { redis?.destroy(); } catch { /* 已断开 */ }
            finish(new Error('owned Redis deadline'));
        }, timeoutMs);
        Promise.resolve().then(action).then((value) => finish(null, value), (error) => finish(error));
    });
}

async function connectRedis() {
    if (redis?.isReady) return;
    try { redis?.destroy(); } catch { /* 此测试此前持有的客户端 */ }
    redis = createClient({ socket: { host: '127.0.0.1', port: 6380, connectTimeout: 2000, reconnectStrategy: false },
        password: redisSecret.password, disableOfflineQueue: true, commandsQueueMaxLength: 8 });
    redis.on('error', () => {}); // 不打印可能含连接详情的错误对象。
    await redisBounded(() => redis.connect());
    assert.equal(await redisBounded(() => redis.ping()), 'PONG', '必须真实Redis可达，不能将模拟TCP当Redis验收');
}

async function openBridge() {
    bridge = createRedisBridge(redisSecret);
    await bridge.start(6380, '127.0.0.1'); // occupied直接失败，不停止/替换已有监听者。
    bridgeOwned = true;
    bridgePaused = false;
}

async function pauseBridge() {
    try { redis?.destroy(); } catch { /* 仅本测试Redis客户端 */ }
    await bridge.close();
    assert.equal(bridge.diagnostics().activeConnections, 0);
    assert.equal(bridge.diagnostics().unconfirmedChildren, 0);
    bridgePaused = true;
}

try {
    assert.equal(process.platform, 'win32');
    assert.ok(process.env.EV_TEST_CONFIG_FILE, '必须明确EV_TEST_CONFIG_FILE');
    const config = await loadConfig(process.env.EV_TEST_CONFIG_FILE);
    assert.equal(config.mysql.database, 'ev_test');
    assert.equal(config.mysql.port, 3310);
    assert.equal(config.mysql.user, 'ev_app');
    assert.ok(['127.0.0.1', 'localhost', '::1'].includes(config.mysql.host));
    redisSecret = await loadRedisConfig(); // 既有私密目的标记/固定127:6380/64hex密码均由已审工具核对。
    stage = 'owned-bridge-and-real-redis';
    await openBridge();
    await connectRedis();
    const ownedKeys = [equipmentId, '..'].map((id) => equipmentCacheKey('ev_test', namespace, id));
    for (const key of ownedKeys) assert.equal(await redisBounded(() => redis.get(key)), null, '新随机namespace应冷缓存');
    stage = 'empty-test-database';
    pool = mysql.createPool({ ...config.mysql, charset: 'utf8mb4', multipleStatements: false });
    const [initial] = await pool.execute('SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE()');
    assert.equal(initial.length, 0, 'ev_test非空不得运行或清表');
    ownedTables = true;
    app = await createApp({ host: '127.0.0.1', port: 0, token: config.token, mysql: config.mysql,
        operationTimeoutMs: config.operationTimeoutMs, closeTimeoutMs: config.closeTimeoutMs,
        redis: { enabled: true, configFile: redisConfigFile, namespace, ttlSeconds: 60, connectTimeoutMs: 2000,
            commandTimeoutMs: 1000, reconnectDelayMs: 150, maxPendingCommands: 8, maxValueBytes: 6291456, closeTimeoutMs: 1000 } });
    const address = await app.start(0, '127.0.0.1');
    const base = `http://127.0.0.1:${address.port}`;
    const request = async (route, { method = 'GET', body, authorization = config.token } = {}) => {
        const started = performance.now();
        const response = await fetch(base + route, { method, headers: {
            ...(authorization === null ? {} : { Authorization: `Bearer ${authorization}` }),
            ...(body === undefined ? {} : { 'Content-Type': 'application/json' }) },
            ...(body === undefined ? {} : { body: JSON.stringify(body) }), signal: AbortSignal.timeout(15000) });
        const result = { status: response.status, cache: response.headers.get('x-cache'), json: await response.json(), milliseconds: performance.now() - started };
        return result;
    };
    const status = async () => {
        const result = await request('/api/status');
        assert.equal(result.status, 200);
        const value = result.json.data;
        assert.ok(value.cache.enabled && value.mysql.ready);
        assert.ok(Number.isInteger(value.mysql.equipmentDetailReadAttempts));
        const diagnostic = JSON.stringify(value);
        assert.ok(!diagnostic.includes(config.token) && !diagnostic.includes(redisSecret.password) && !diagnostic.includes(namespace), '诊断不得含秘密/namespace');
        return value;
    };
    const detailRoute = (id) => '/api/equipment?id=' + encodeURIComponent(id);
    stage = 'http-cold-hot-and-authorization';
    for (const id of [equipmentId, '..']) assert.equal((await request('/api/equipment', { method: 'POST', body: fixture(id) })).status, 201);
    const before = await status();
    const cold = await request(detailRoute(equipmentId));
    assert.equal(cold.status, 200);
    assert.equal(cold.cache, 'MISS');
    assert.deepEqual(cold.json.data, fixture());
    const afterCold = await status();
    assert.equal(afterCold.mysql.equipmentDetailReadAttempts, before.mysql.equipmentDetailReadAttempts + 1);
    assert.equal(afterCold.cache.misses, before.cache.misses + 1);
    const hot = await request(detailRoute(equipmentId));
    assert.equal(hot.cache, 'HIT');
    assert.deepEqual(hot.json.data, cold.json.data);
    const afterHot = await status();
    assert.equal(afterHot.mysql.equipmentDetailReadAttempts, afterCold.mysql.equipmentDetailReadAttempts);
    assert.equal(afterHot.cache.hits, afterCold.cache.hits + 1);
    const alias = await request('/api/equipment/' + encodeURIComponent(equipmentId));
    assert.equal(alias.cache, 'HIT');
    assert.deepEqual(alias.json.data, fixture());
    const dotCold = await request(detailRoute('..'));
    const dotHot = await request(detailRoute('..'));
    assert.equal(dotCold.cache, 'MISS');
    assert.equal(dotHot.cache, 'HIT');
    assert.deepEqual(dotHot.json.data, fixture('..'));
    timing.push({ phase: 'cold', milliseconds: Number(cold.milliseconds.toFixed(3)), parts: 2 },
        { phase: 'hot', milliseconds: Number(hot.milliseconds.toFixed(3)), parts: 2 });
    const beforeUnauthorized = await status();
    for (const authorization of [null, 'invalid-cache-test-token']) {
        const rejected = await request(detailRoute(equipmentId), { authorization });
        assert.equal(rejected.status, 401);
        assert.equal(rejected.json.error.code, 'UNAUTHORIZED');
        assert.equal(rejected.json.data, undefined);
        assert.equal(rejected.cache, null);
        assert.equal((await request('/api/status', { authorization })).status, 401);
    }
    const afterUnauthorized = await status();
    assert.deepEqual(cacheCounters(afterUnauthorized), cacheCounters(beforeUnauthorized));
    assert.equal(afterUnauthorized.mysql.equipmentDetailReadAttempts, beforeUnauthorized.mysql.equipmentDetailReadAttempts);
    const created = await request('/api/annotations', { method: 'POST', body: { equipmentId, partId, text: initialNote, author: '工艺甲', requestId: noteId } });
    assert.equal(created.status, 201);
    assert.equal(created.cache, null);
    console.log(`CACHE_HTTP cold=MISS hot=HIT exactAlias=1 exactDotId=1 mysqlReadsColdDelta=1 mysqlReadsHotDelta=0 unauthorizedBeforeCache=1 timings=${JSON.stringify(timing)}`);

    stage = 'gui-proxy-and-controls';
    temporary = await mkdtemp(path.join(tmpdir(), 'equipment-cache-user-'));
    proxy = http.createServer(async (incoming, outgoing) => {
        try {
            const chunks = [];
            let received = 0;
            for await (const chunk of incoming) {
                received += chunk.length;
                if (received > 5 * 1024 * 1024) throw new Error('bounded test request');
                chunks.push(chunk);
            }
            const forwarded = http.request({ host: '127.0.0.1', port: address.port, path: incoming.url,
                method: incoming.method, headers: { ...incoming.headers, host: `127.0.0.1:${address.port}` } });
            forwarded.setTimeout(15000, () => forwarded.destroy());
            outgoing.once('close', () => { if (!outgoing.writableEnded) forwarded.destroy(); });
            const response = await new Promise((resolve, reject) => {
                forwarded.once('response', resolve);
                forwarded.once('error', reject);
                forwarded.end(Buffer.concat(chunks));
            });
            const returned = [];
            let size = 0;
            for await (const chunk of response) {
                size += chunk.length;
                if (size > 6 * 1024 * 1024) throw new Error('bounded test response');
                returned.push(chunk);
            }
            const bytes = Buffer.concat(returned);
            const url = new URL(incoming.url, base);
            const isDetail = incoming.method === 'GET' && url.pathname === '/api/equipment' && url.searchParams.has('id');
            if (isDetail && response.statusCode === 200) {
                assert.deepEqual(JSON.parse(bytes.toString('utf8')).data, fixture(url.searchParams.get('id')));
                observations.push({ cache: response.headers['x-cache'], down: cacheDown, restored: cacheRestored });
            }
            if (url.pathname === '/api/annotations') assert.equal(response.headers['x-cache'], undefined, '批注不得缓存');
            if (!outgoing.destroyed) {
                outgoing.writeHead(response.statusCode, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': bytes.length,
                    ...(response.headers['x-cache'] ? { 'X-Cache': response.headers['x-cache'] } : {}) });
                outgoing.end(bytes);
            }
        } catch (error) {
            controlFailure = error;
            if (!outgoing.destroyed && !outgoing.writableEnded) {
                outgoing.writeHead(503, { 'Content-Type': 'application/json' });
                outgoing.end(JSON.stringify({ error: { code: 'TEST_FORWARD_FAILURE', message: '测试转发未完成。' } }));
            }
        }
    });
    proxy.on('connection', (socket) => { proxySockets.add(socket); socket.once('close', () => proxySockets.delete(socket)); });
    await new Promise((resolve, reject) => {
        proxy.once('error', reject);
        proxy.listen(0, '127.0.0.1', () => { proxy.off('error', reject); resolve(); });
    });
    const noteRoute = '/api/annotations?equipmentId=' + encodeURIComponent(equipmentId) + '&partId=' + encodeURIComponent(partId);
    const operations = {
        'cache-down': async () => { downBefore = await status(); await pauseBridge(); cacheDown = true; },
        'cache-fault-check': async () => {
            const value = await status();
            assert.ok(value.mysql.equipmentDetailReadAttempts > downBefore.mysql.equipmentDetailReadAttempts);
            assert.ok(observations.some((item) => item.down && item.cache === 'BYPASS'));
            assert.equal(value.cache.state, 'degraded');
        },
        'peer-update': async () => {
            peerBefore = await status();
            const result = await request('/api/annotations/' + noteId, { method: 'PUT', body: { text: latestNote, editor: '工艺乙', expectedRevision: 1 } });
            assert.equal(result.status, 200);
            assert.equal(result.cache, null);
            assert.equal(result.json.data.revision, 2);
            const value = await status();
            assert.deepEqual(cacheCounters(value), cacheCounters(peerBefore));
            peerCacheCounters = cacheCounters(value);
        },
        'annotation-check': async () => {
            const value = await status();
            assert.deepEqual(cacheCounters(value), peerCacheCounters);
            const latest = await request(noteRoute);
            assert.equal(latest.cache, null);
            assert.equal(latest.json.data[0].revision, 2);
            assert.equal(latest.json.data[0].text, latestNote);
        },
        'cache-up': async () => {
            await openBridge();
            const deadline = performance.now() + 7000;
            let recovered;
            do {
                recovered = await request(detailRoute(equipmentId));
                assert.equal(recovered.status, 200);
                assert.deepEqual(recovered.json.data, fixture());
                if (recovered.cache === 'HIT') break;
                await pause(160);
            } while (performance.now() < deadline);
            assert.equal(recovered.cache, 'HIT', '恢复本桥后应有界重连，Linux缓存仍存续');
            cacheDown = false;
            cacheRestored = true;
        },
        'cache-recovery-check': async () => {
            const value = await status();
            assert.equal(value.cache.state, 'ready');
            assert.ok(observations.some((item) => item.restored && item.cache === 'HIT'));
        }
    };
    poll = setInterval(() => {
        if (controlTask) return;
        controlTask = (async () => {
            for (const [name, operation] of Object.entries(operations)) {
                if (!(await exists(path.join(temporary, name + '.request'))) || await exists(path.join(temporary, name + '.done'))) continue;
                await operation();
                await writeFile(path.join(temporary, name + '.done'), 'verified');
            }
            controlTask = null;
        })().catch((error) => { controlFailure = error; });
    }, 20);
    const guiConfig = path.join(temporary, 'client.local.json');
    await writeFile(guiConfig, JSON.stringify({ host: '127.0.0.1', port: proxy.address().port, token: config.token }), { mode: 0o600 });
    const platform = process.argv[3] ?? 'offscreen';
    assert.ok(['offscreen', 'windows'].includes(platform));
    const executable = path.resolve(process.argv[2] ?? path.join(root, 'build/bin/cache_user_tests.exe'));
    const snapshotDir = path.resolve(process.argv[4] ?? path.join(root, `build/cache-user-0.6-${platform}`));
    // 复用已fake回归的有界子进程close确认，不把kill调用当Qt退出证据。
    await runRestoreCli(executable, ['-platform', platform, '--snapshot-dir', snapshotDir], async (process) => {
        process.stdin.end();
    }, {
        spawnProcess: (command, arguments_, options) => {
            child = spawn(command, arguments_, { ...options, cwd: root, env: { ...process.env,
                EQUIPMENT_SERVICE_CONFIG: guiConfig, EQUIPMENT_CACHE_CONTROL_DIR: temporary, EQUIPMENT_CACHE_NOTE_ID: noteId } });
            child.stdout.on('data', (data) => process.stdout.write(data));
            child.stderr.on('data', (data) => process.stderr.write(data));
            return child;
        },
        onUnconfirmed: ({ pid, executable: ownPath }) => {
            guiExitUnconfirmed = true;
            console.error(`CACHE_USER guiExitConfirmed=0 ownedPid=${pid} executable=${ownPath}；保留本轮ev_test。`);
        }
    });
    if (controlFailure) throw new Error('controlled GUI observation failed');
    for (const name of Object.keys(operations)) assert.ok(await exists(path.join(temporary, name + '.done')));
    const [persisted] = await pool.execute('SELECT equipment_id,part_id,revision,text,author,updated_by FROM annotations WHERE id=?', [noteId]);
    assert.equal(persisted.length, 1);
    assert.equal(persisted[0].revision, 2);
    assert.equal(persisted[0].text, latestNote);
    assert.equal(persisted[0].equipment_id, equipmentId);
    assert.equal(persisted[0].part_id, partId);
    assert.equal(persisted[0].author, '工艺甲');
    assert.equal(persisted[0].updated_by, '工艺乙');
    console.log('CACHE_USER accepted=1 httpMissHit=1 actualMysqlCounts=1 authBeforeCache=1 gui=1 bypassDraft=1 latestNotesMysql=1 recovered=1 simulated=1');
} catch {
    console.error(`CACHE_USER accepted=0 stage=${stage}；当前真实Redis/GUI未全部通过，不把纯检查或模拟网络当实测。`);
    process.exitCode = 1;
} finally {
    clearInterval(poll);
    try {
        await controlTask;
        await app?.close();
        if (proxy) {
            for (const socket of proxySockets) socket.destroy();
            if (proxy.listening) await new Promise((resolve) => proxy.close(resolve));
        }
        if (bridgeOwned) {
            if (bridgePaused) await openBridge();
            await connectRedis();
            // 只删除明确的本次ev_test+随机namespace+精确IDhash键，不scan、不FLUSH。
            for (const id of [equipmentId, '..']) await redisBounded(() => redis.del(equipmentCacheKey('ev_test', namespace, id)));
            try { redis.destroy(); } catch { /* 本次客户端已结束 */ }
            await bridge.close();
            assert.equal(bridge.diagnostics().activeConnections, 0);
            assert.equal(bridge.diagnostics().unconfirmedChildren, 0);
        }
        if (guiExitUnconfirmed) {
            console.error('CACHE_USER cleanup=0 ownedTestTablesKept=1；Qt退出未确认，不发DROP。');
            process.exitCode = 1;
        } else if (ownedTables) {
            for (const table of ['annotations', 'part', 'equipment', 'schema_version']) await pool.execute(`DROP TABLE IF EXISTS ${table}`);
            const [remaining] = await pool.execute('SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE()');
            assert.equal(remaining.length, 0);
        }
        if (!guiExitUnconfirmed) console.log('CACHE_USER cleanup=1 ownedKeysRemoved=1 ownedBridgeChildrenConfirmed=1 ownedTestTablesRemoved=1');
    } catch {
        console.error('CACHE_USER cleanup=0；本轮收尾未确认，检查本轮测试资源，禁止停正常服务或清其他缓存。');
        process.exitCode = 1;
    } finally {
        try { await pool?.end(); }
        finally {
            if (temporary) {
                const target = path.resolve(temporary);
                assert.equal(path.dirname(target), path.resolve(tmpdir()));
                assert.ok(path.basename(target).startsWith('equipment-cache-user-'));
                await rm(target, { recursive: true, force: true });
            }
        }
    }
}
