import test from 'node:test';
import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { createClient, RESP_TYPES } from '@redis/client';
import { createEquipmentCache, equipmentCacheKey, boundedGetScript } from '../src/cache.mjs';
import { createApp } from '../src/server.mjs';
import { createRedisBridge } from '../tools/redis-bridge.mjs';
import { loadRedisConfig } from '../tools/setup-redis.mjs';

const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// 检查用client也关闭实际socket，不仅race返回；错误统一安全化，不输出secret/key/命令内容。
function bounded(client, operation, timeoutMs = 8000) {
    return new Promise((resolve, reject) => {
        let settled = false;
        const finish = (error, result) => {
            if (settled) return;
            settled = true; clearTimeout(timer);
            if (error) reject(new Error('Real Redis technical command failed')); else resolve(result);
        };
        const timer = setTimeout(() => { client.destroy(); finish(new Error('deadline')); }, timeoutMs);
        Promise.resolve().then(operation).then((value) => finish(null, value), (error) => finish(error));
    });
}
async function inspectorFor(config) {
    const client = createClient({ socket: { host: '127.0.0.1', port: 6380, reconnectStrategy: false, connectTimeout: 5000 },
        password: config.password, RESP: 2, disableOfflineQueue: true, commandsQueueMaxLength: 8, disableClientInfo: true });
    client.on('error', () => {});
    try { await bounded(client, () => client.connect()); return client; }
    catch { client.destroy(); throw new Error('Real Redis inspector connection failed'); }
}

test('真实Redis7.4.7：TTL/非法及巨型值/类型Lua/不负缓存/桥恢复与确认close', { timeout: 90000 }, async (t) => {
    assert.equal(process.platform, 'win32', '此脚本只用已核实的Windows/WSL专属环境');
    const privateConfig = await loadRedisConfig(); // 只读忽略秘密，不打印内容、不改service/test。
    assert.equal(privateConfig.distribution, 'Ubuntu-24.04');
    const namespace = 'backend-real-' + randomBytes(12).toString('hex');
    const databaseTag = 'ev_test'; // 仅key隔离标签；本脚本不建立任何MySQL连接。
    const id = '真实技术夹具A ';
    const missingId = '不存在的技术夹具';
    const guardId = '定向删除保护夹具';
    const ownedKeys = [id, missingId, guardId].map((value) => equipmentCacheKey(databaseTag, namespace, value));
    const [key, missingKey, guardKey] = ownedKeys;
    const value = { equipmentId: id, equipmentName: '真实Redis虚构设备', parts: [
        { id: '同名/a ', name: '同名', material: '钢', shape: 'box', dimensions: [1, 2, 3], position: [0, 1, 2] }
    ] };
    const settings = { enabled: true, namespace, ttlSeconds: 1, maxValueBytes: 1024,
        connectTimeoutMs: 5000, commandTimeoutMs: 5000, reconnectDelayMs: 100, closeTimeoutMs: 1000 };
    let bridge;
    let inspector;
    let cache;
    let app;
    let cleanupPromise;
    let cleanupConfirmed = false;
    const inspect = (args) => bounded(inspector, () => inspector.withTypeMapping({ [RESP_TYPES.BLOB_STRING]: Buffer }).sendCommand(args));
    const cleanup = () => cleanupPromise ??= (async () => {
        let failed = false;
        try {
            if (bridge?.server.listening) {
                if (!inspector?.isReady) { inspector?.destroy(); inspector = await inspectorFor(privateConfig); }
                // 明确固定3个本轮key，不scan/FLUSH，不碰其他namespace或真实资料。
                await inspect(['DEL', ...ownedKeys]);
                assert.equal(await inspect(['EXISTS', ...ownedKeys]), 0);
            } else if (cache) failed = true;
        } catch { failed = true; }
        try { if (app) await app.close(); else await cache?.close(); } catch { failed = true; }
        inspector?.destroy();
        try { await bridge?.close(); } catch { failed = true; }
        if (bridge && (bridge.diagnostics().activeConnections !== 0 || bridge.diagnostics().unconfirmedChildren !== 0 || bridge.server.listening)) failed = true;
        if (failed) throw new Error('Real Redis owned-key or owned-bridge cleanup unconfirmed');
        cleanupConfirmed = true;
    })();
    t.after(cleanup);

    bridge = createRedisBridge(privateConfig);
    await bridge.start(); // 未知占用端口直接失败；只创建/关闭本次桥，不停LinuxRedis。
    inspector = await inspectorFor(privateConfig);
    assert.equal((await inspect(['PING'])).toString(), 'PONG');
    const info = (await inspect(['INFO', 'server'])).toString('utf8');
    assert.equal(/^redis_version:(.+)$/m.exec(info)?.[1].trim(), '7.4.7');
    cache = await createEquipmentCache(settings, databaseTag);
    assert.equal((await cache.lookup(id)).mode, 'MISS');
    assert.equal(await cache.fill(id, value), true);
    const ttl = await inspect(['PTTL', key]);
    assert.ok(ttl > 0 && ttl <= 1000, '真实SET EX1有正且不超过1秒的剩余寿命');
    const hit = await cache.lookup(id);
    assert.equal(hit.mode, 'HIT'); assert.deepEqual(hit.document, value);
    await wait(1200);
    assert.equal(await inspect(['EXISTS', key]), 0, '实际Redis TTL到期，不以假时钟替代');
    assert.equal((await cache.lookup(id)).mode, 'MISS');
    await inspect(['SET', guardKey, 'guard-value', 'EX', '30']);

    const wrongGeometry = structuredClone(value); wrongGeometry.parts[0].dimensions[0] = 0;
    const wrongId = { ...value, equipmentId: '其他设备ID' };
    for (const bytes of [Buffer.from('{bad JSON'), Buffer.from([0xc3, 0x28]),
        Buffer.from(JSON.stringify(wrongGeometry)), Buffer.from(JSON.stringify(wrongId))]) {
        await inspect(['SET', key, bytes, 'EX', '30']);
        assert.equal((await cache.lookup(id)).mode, 'MISS');
        assert.equal(await inspect(['EXISTS', key]), 0);
        assert.equal((await inspect(['GET', guardKey])).toString(), 'guard-value');
    }
    await inspect(['LPUSH', key, 'list-value']); await inspect(['EXPIRE', key, '30']);
    assert.equal((await cache.lookup(id)).mode, 'MISS');
    assert.equal(await inspect(['EXISTS', key]), 0, '真实LIST由TYPE路径拒绝并只删目标key');
    const oversized = Buffer.alloc(6 * 1024 * 1024 + 1, 120);
    await inspect(['SET', key, oversized, 'EX', '30']);
    const boundedReply = await inspect(['EVAL', boundedGetScript, '1', key, String(settings.maxValueBytes)]);
    assert.equal(Array.isArray(boundedReply) && boundedReply.length === 1 && boundedReply[0] === 2, true,
        '真实Lua超限只返回状态，不把巨型payload返回parser');
    assert.equal(await inspect(['EXISTS', key]), 0);
    await inspect(['SET', key, oversized, 'EX', '30']);
    assert.equal((await cache.lookup(id)).mode, 'MISS');
    assert.equal(await inspect(['EXISTS', key]), 0);
    assert.equal((await inspect(['GET', guardKey])).toString(), 'guard-value');
    assert.equal(cache.status().invalidValues, 6);

    await bridge.close();
    assert.equal(bridge.diagnostics().activeConnections, 0);
    assert.equal((await cache.lookup(id)).mode, 'BYPASS');
    inspector.destroy();
    bridge = createRedisBridge(privateConfig); await bridge.start();
    inspector = await inspectorFor(privateConfig); await wait(150);
    assert.equal((await cache.lookup(id)).mode, 'MISS');
    assert.equal(await cache.fill(id, value), true);
    assert.equal((await cache.lookup(id)).mode, 'HIT');

    // 不负缓存通过真实Redis+HTTP验证；Repository只返回受控空结果，不访问MySQL/ev_test。
    app = await createApp({ token: 'technical-test-token', mysql: { database: databaseTag }, redis: settings }, { cache,
        repository: { initialize: async () => {}, close: async () => {}, health: async () => {}, get: async () => null } });
    const address = await app.start(0);
    const setsBefore404 = cache.status().sets;
    for (let index = 0; index < 2; ++index) {
        const response = await fetch(`http://127.0.0.1:${address.port}/api/equipment?id=${encodeURIComponent(missingId)}`,
            { headers: { Authorization: 'Bearer technical-test-token' }, signal: AbortSignal.timeout(10000) });
        assert.equal(response.status, 404); await response.arrayBuffer();
    }
    assert.equal(await inspect(['EXISTS', missingKey]), 0);
    assert.equal(cache.status().sets, setsBefore404);
    assert.equal(cache.status().inFlight, 0);
    const counters = cache.status();
    await cleanup();
    assert.equal(cleanupConfirmed, true);
    console.log(`Redis7.4.7真实技术验收通过：TTL1/坏JSON UTF8几何ID/LIST/6MiB超限原子Lua/定向删除/不负缓存/恢复；invalidValues=${counters.invalidValues}，ownedKeys=0，bridgeChildren=0，cleanup=1。`);
});
