import test from 'node:test';
import assert from 'node:assert/strict';
import { createClient } from '@redis/client';
import { createEquipmentCache, equipmentCacheKey } from '../src/cache.mjs';
import { createApp } from '../src/server.mjs';
import { ApiError } from '../src/validation.mjs';
import { createRespCachePeer } from './resp-cache-peer.mjs';

const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const document = (id = 'A') => ({ equipmentId: id, equipmentName: '中文', parts: [] });
const options = { enabled: true, namespace: 'controlled-network', connectTimeoutMs: 50, commandTimeoutMs: 50,
    reconnectDelayMs: 100, closeTimeoutMs: 50, maxValueBytes: 512 };
function dependencies(peer) { return { credentialsReader: async () => ({ host: '127.0.0.1', port: 6380, password: '0'.repeat(64) }),
    clientFactory: (config) => createClient({ ...config, socket: { ...config.socket, port: peer.port } }) }; }
async function awaitSocketsClosed(peer) {
    for (let index = 0; index < 100 && peer.sockets.size; ++index) await wait(5);
    assert.equal(peer.sockets.size, 0, '真实官方客户端TCP socket应退出');
}

test('官方client+受控RESP MISS/SET/HIT，巨型值不返回parser，坏值定向删除', { timeout: 5000 }, async (t) => {
    const peer = await createRespCachePeer();
    const cache = await createEquipmentCache(options, 'ev_test', dependencies(peer));
    t.after(async () => { await cache.close(); await peer.close(); });
    assert.equal((await cache.lookup('A')).mode, 'MISS');
    assert.equal(await cache.fill('A', document()), true);
    assert.deepEqual((await cache.lookup('A')).document, document());
    assert.equal(cache.status().hits, 1);
    const key = equipmentCacheKey('ev_test', options.namespace, 'A');
    peer.entries.set(key, { type: 'string', bytes: Buffer.alloc(1024 * 1024), expires: Infinity });
    assert.equal((await cache.lookup('A')).mode, 'MISS');
    assert.equal(peer.entries.has(key), false);
    assert.ok(peer.maximumValueSent < 512);
    peer.entries.set(key, { type: 'string', bytes: Buffer.from([255]), expires: Infinity });
    assert.equal((await cache.lookup('A')).mode, 'MISS');
    assert.equal(peer.entries.has(key), false);
    assert.equal(cache.status().invalidValues, 2);
    await cache.close();
    await awaitSocketsClosed(peer);
});

test('握手/已发命令hang真正毁socket，禁后台重连且后续请求冷却恢复', { timeout: 5000 }, async (t) => {
    const peer = await createRespCachePeer();
    const cache = await createEquipmentCache(options, 'ev_test', dependencies(peer));
    t.after(async () => { await cache.close(); await peer.close(); });
    peer.authHang = true;
    assert.equal((await cache.lookup('A')).mode, 'BYPASS');
    assert.equal(cache.status().lastFailure, 'connect_timeout');
    await awaitSocketsClosed(peer);
    const count = peer.connections;
    await wait(120);
    assert.equal(peer.connections, count, '没有自动后台重连');
    peer.authHang = false; peer.evalHang = true;
    assert.equal((await cache.lookup('A')).mode, 'BYPASS');
    assert.equal(cache.status().lastFailure, 'command_timeout');
    assert.equal(cache.status().inFlight, 0);
    await awaitSocketsClosed(peer);
    peer.evalHang = false; await wait(120);
    assert.equal((await cache.lookup('A')).mode, 'MISS');
    assert.equal(cache.status().ready, true);
});

test('官方client未完成命令时close有界清队列与真实socket', { timeout: 5000 }, async (t) => {
    const peer = await createRespCachePeer(); peer.evalHang = true;
    const cache = await createEquipmentCache({ ...options, commandTimeoutMs: 1000 }, 'ev_test', dependencies(peer));
    t.after(async () => { await cache.close(); await peer.close(); });
    const pending = cache.lookup('A');
    for (let index = 0; index < 100 && peer.evals === 0; ++index) await wait(5);
    assert.equal(peer.evals, 1);
    const started = Date.now(); await cache.close();
    assert.ok(Date.now() - started < 500);
    assert.equal((await pending).mode, 'BYPASS');
    assert.equal(cache.status().inFlight, 0);
    await awaitSocketsClosed(peer);
});

test('HTTP鉴权/ID先于缓存，X-Cache/status真实get计数，批注永不缓存，SET失败不改成功', { timeout: 5000 }, async (t) => {
    const peer = await createRespCachePeer();
    let databaseReady = true;
    let revision = 1;
    const store = { initialize: async () => {}, close: async () => {},
        health: async () => { if (!databaseReady) throw new ApiError(503, 'DATABASE_UNAVAILABLE', '受控不可用'); },
        get: async (id) => id === 'missing' ? null : document(id), list: async () => [],
        listAnnotations: async () => [{ revision }],
        updateAnnotation: async (_id, value) => { revision += 1; return { revision, text: value.text }; } };
    const app = await createApp({ token: 'fixture-token', mysql: { database: 'ev_test' }, redis: options },
        { repository: store, cacheDependencies: dependencies(peer) });
    t.after(async () => { await app.close(); await peer.close(); });
    const address = await app.start(0); const base = `http://127.0.0.1:${address.port}`;
    const request = async (route, extra = {}) => {
        const response = await fetch(base + route, { ...extra, headers: { Authorization: 'Bearer fixture-token', ...extra.headers } });
        return { status: response.status, cache: response.headers.get('x-cache'), json: await response.json() };
    };
    assert.equal((await fetch(base + '/api/status')).status, 401);
    assert.equal((await fetch(base + '/api/equipment?id=A')).status, 401);
    assert.equal((await request('/api/equipment?id=' + 'a'.repeat(129))).status, 400);
    assert.equal(peer.connections, 0);
    assert.equal((await request('/api/status')).json.data.cache.bypass, 0);
    assert.equal((await request('/api/equipment?id=A')).cache, 'MISS');
    assert.equal((await request('/api/equipment/A')).cache, 'HIT');
    const status = (await request('/api/status')).json.data;
    assert.equal(status.mysql.equipmentDetailReadAttempts, 1);
    assert.deepEqual([status.cache.hits, status.cache.misses, status.cache.sets], [1, 1, 1]);
    for (const privateText of ['fixture-token', 'namespace', 'password', 'configFile', 'ev:geometry:'])
        assert.equal(JSON.stringify(status).includes(privateText), false);
    const evalCount = peer.evals;
    await request('/api/annotations?equipmentId=A');
    await request('/api/annotations/00000000-0000-0000-0000-000000000000', { method: 'PUT',
        headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ text: '新修订', editor: '甲', expectedRevision: 1 }) });
    assert.equal((await request('/api/annotations?equipmentId=A')).json.data[0].revision, 2);
    assert.equal(peer.evals, evalCount, '最新批注不经过cache');
    assert.equal((await request('/api/equipment?id=missing')).status, 404);
    assert.equal((await request('/api/equipment?id=missing')).status, 404);
    assert.equal(peer.entries.has(equipmentCacheKey('ev_test', options.namespace, 'missing')), false, '404不负缓存');
    peer.setFail = true;
    const miss = await request('/api/equipment?id=B');
    assert.equal(miss.status, 200); assert.equal(miss.cache, 'MISS');
    assert.deepEqual(miss.json.data, document('B'));
    assert.equal((await request('/api/equipment?id=C')).cache, 'BYPASS');
    databaseReady = false;
    const down = await request('/api/status');
    assert.equal(down.status, 200); assert.equal(down.json.data.mysql.ready, false);
    assert.equal((await request('/health')).status, 503);
});
