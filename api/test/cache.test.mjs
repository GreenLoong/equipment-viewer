import test from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { createEquipmentCache, EquipmentCache, equipmentCacheKey, normalizeCacheOptions } from '../src/cache.mjs';

const document = (id = 'A') => ({ equipmentId: id, equipmentName: '中文设备', parts: [
    { id: 'a ', name: '同名', material: '钢', shape: 'box', dimensions: [1, 2, 3], position: [0, 1, 2] }
] });
const credentials = { host: '127.0.0.1', port: 6380, password: '0'.repeat(64) };
const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
class FakeClient extends EventEmitter {
    constructor(entries = new Map(), now = () => Date.now()) {
        super(); this.entries = entries; this.now = now; this.isReady = false;
        this.connects = 0; this.destroys = 0; this.commands = []; this.setFail = false;
    }
    async connect() { this.connects += 1; this.isReady = true; }
    destroy() { this.destroys += 1; this.isReady = false; this.rejectPending?.(new Error('destroyed')); }
    withTypeMapping() { return this; }
    async sendCommand(args) {
        this.commands.push(args[0]);
        const key = args[0] === 'EVAL' ? args[3] : args[1];
        if (args[0] === 'EVAL') {
            assert.ok(args[1].includes("redis.call('TYPE'"));
            assert.ok(args[1].indexOf("redis.call('STRLEN'") < args[1].lastIndexOf("redis.call('GET'"));
            const entry = this.entries.get(key);
            if (!entry || entry.expires <= this.now()) { this.entries.delete(key); return [0]; }
            if (entry.type !== 'string' || entry.bytes.length > Number(args[4])) { this.entries.delete(key); return [2]; }
            return [1, entry.bytes];
        }
        if (args[0] === 'DEL') { this.entries.delete(key); return 1; }
        if (args[0] === 'SET') {
            if (this.setFail) throw new Error('controlled SET failure');
            assert.equal(args[3], 'EX');
            this.entries.set(key, { type: 'string', bytes: Buffer.from(args[2]), expires: this.now() + Number(args[4]) * 1000 });
            return 'OK';
        }
        throw new Error('unexpected command');
    }
}
function dependencies(client, extra = {}) { return { credentialsReader: async () => credentials,
    clientFactory: (options) => { assert.equal(options.disableOfflineQueue, true); assert.equal(options.socket.reconnectStrategy, false); return client; }, ...extra }; }

test('缓存禁用不读取秘密/不创建连接；结构和精确key分隔严格', async () => {
    const cache = await createEquipmentCache(undefined, 'ev_test', { credentialsReader: () => assert.fail('disabled read'), clientFactory: () => assert.fail('disabled connection') });
    assert.equal((await cache.lookup('A')).mode, 'BYPASS');
    assert.equal(cache.status().state, 'disabled');
    assert.throws(() => normalizeCacheOptions(null));
    for (const value of [{ enabled: 'true' }, { ttlSeconds: 0 }, { namespace: 'space name' }, { maxPendingCommands: 33 }, { configFile: '' }, { enable: true }])
        assert.throws(() => normalizeCacheOptions(value));
    assert.equal(normalizeCacheOptions().ttlSeconds, 60);
    const ids = ['A', 'a', 'a ', '.', '..', '中文/+', '😀'];
    assert.equal(new Set(ids.map((id) => equipmentCacheKey('ev_test', 'n', id))).size, ids.length);
    assert.notEqual(equipmentCacheKey('ev_test', 'n', 'A'), equipmentCacheKey('ev_shared', 'n', 'A'));
    assert.notEqual(equipmentCacheKey('ev_test', 'n', 'A'), equipmentCacheKey('ev_test', 'other', 'A'));
    await assert.rejects(cache.lookup(' '));
    assert.equal(cache.status().bypass, 1, 'invalid ID无新增计数');
    await cache.close();
});

test('纯缓存MISS/SET EX/HIT/TTL，一次lookup只计一个结果', async () => {
    let clock = 0;
    const client = new FakeClient(new Map(), () => clock);
    const cache = await createEquipmentCache({ enabled: true }, 'ev_test', dependencies(client, { now: () => clock }));
    assert.equal((await cache.lookup('A')).mode, 'MISS');
    assert.equal(await cache.fill('A', document()), true);
    const hit = await cache.lookup('A');
    assert.equal(hit.mode, 'HIT'); assert.deepEqual(hit.document, document());
    clock = 60001;
    assert.equal((await cache.lookup('A')).mode, 'MISS');
    assert.deepEqual([cache.status().hits, cache.status().misses, cache.status().bypass, cache.status().sets], [1, 2, 0, 1]);
    await cache.close();
    assert.equal(client.isReady, false);
});

test('坏JSON/UTF8/几何/错ID/巨型/类型只删除本key并MISS，不碰其他namespace', async () => {
    const entries = new Map();
    const client = new FakeClient(entries);
    const cache = await createEquipmentCache({ enabled: true, maxValueBytes: 512 }, 'ev_test', dependencies(client));
    const key = equipmentCacheKey('ev_test', 'geometry-v1', 'A');
    const untouched = equipmentCacheKey('ev_test', 'other', 'A');
    entries.set(untouched, { type: 'string', bytes: Buffer.from('keep'), expires: Infinity });
    const badGeometry = document(); badGeometry.parts[0].dimensions[0] = 0;
    for (const [type, bytes] of [['string', Buffer.from('{bad')], ['string', Buffer.from([255])],
        ['string', Buffer.from(JSON.stringify(document('other')))], ['string', Buffer.from(JSON.stringify(badGeometry))],
        ['string', Buffer.alloc(513)], ['list', Buffer.from('wrong type')]]) {
        entries.set(key, { type, bytes, expires: Infinity });
        assert.equal((await cache.lookup('A')).mode, 'MISS');
        assert.equal(entries.has(key), false);
        assert.equal(entries.has(untouched), true);
    }
    assert.equal(cache.status().invalidValues, 6);
    assert.equal(client.commands.some((command) => command.startsWith('FLUSH')), false);
    await cache.close();
});

test('SET失败只返回false，容量不毁其他连接，共享一次connect尝试', async () => {
    const client = new FakeClient();
    let ready;
    client.connect = () => { client.connects += 1; return new Promise((resolve) => { ready = () => { client.isReady = true; resolve(); }; }); };
    const cache = await createEquipmentCache({ enabled: true, maxPendingCommands: 2 }, 'ev_test', dependencies(client));
    const first = cache.lookup('A'); const second = cache.lookup('B');
    assert.equal((await cache.lookup('C')).mode, 'BYPASS');
    await wait(0); ready();
    assert.equal((await first).mode, 'MISS'); assert.equal((await second).mode, 'MISS');
    assert.equal(client.connects, 1);
    assert.equal(client.destroys, 0);
    client.setFail = true;
    assert.equal(await cache.fill('A', document()), false);
    assert.equal(cache.status().sets, 0);
    assert.equal(cache.status().inFlight, 0);
    await cache.close();
});

test('连接/命令hang销毁，晚ready不能重活，close在connect尚未发送时不新建socket', async () => {
    const client = new FakeClient();
    let lateReady;
    client.connect = () => { client.connects += 1; return new Promise((resolve) => { lateReady = () => { client.isReady = true; resolve(); }; }); };
    const cache = await createEquipmentCache({ enabled: true, connectTimeoutMs: 50 }, 'ev_test', dependencies(client));
    assert.equal((await cache.lookup('A')).mode, 'BYPASS');
    assert.equal(cache.status().lastFailure, 'connect_timeout');
    assert.ok(client.destroys >= 1); lateReady(); await wait(0);
    assert.equal(client.isReady, false);
    await cache.close();
    const neverConnected = new FakeClient();
    const closed = await createEquipmentCache({ enabled: true }, 'ev_test', dependencies(neverConnected));
    const pending = closed.lookup('A');
    await closed.close();
    assert.equal((await pending).mode, 'BYPASS');
    assert.equal(neverConnected.connects, 0);
    assert.equal(closed.status().inFlight, 0);
    const hanging = new FakeClient();
    hanging.sendCommand = () => new Promise((_resolve, reject) => { hanging.rejectPending = reject; });
    const timeout = await createEquipmentCache({ enabled: true, commandTimeoutMs: 50 }, 'ev_test', dependencies(hanging));
    assert.equal((await timeout.lookup('A')).mode, 'BYPASS');
    assert.equal(timeout.status().lastFailure, 'command_timeout');
    assert.ok(hanging.destroys >= 1); assert.equal(timeout.status().inFlight, 0);
    await timeout.close();
});

test('秘密文件缺失降级与后续冷却重读恢复，挂文件读可取消且close有界', async () => {
    let clock = 0;
    let available = false;
    const client = new FakeClient();
    const cache = await createEquipmentCache({ enabled: true, reconnectDelayMs: 100 }, 'ev_test', dependencies(client, {
        now: () => clock, credentialsReader: async () => { if (!available) throw new Error('missing'); return credentials; }
    }));
    assert.equal(cache.status().lastFailure, 'config_unavailable');
    assert.equal((await cache.lookup('A')).mode, 'BYPASS');
    available = true; clock = 101;
    assert.equal((await cache.lookup('A')).mode, 'MISS');
    await cache.close();
    let canceled = false;
    const blocked = new EquipmentCache({ enabled: true, connectTimeoutMs: 50 }, 'ev_test', { credentialsReader: (_filename, { signal }) =>
        new Promise((_resolve, reject) => signal.addEventListener('abort', () => { canceled = true; reject(new Error('canceled')); })) });
    await blocked.initialize();
    assert.equal(canceled, true);
    assert.equal(blocked.status().lastFailure, 'config_unavailable');
    await blocked.close();
});

test('请求AbortSignal取消冷秘密读取，reader收到abort且不创建连接', async (t) => {
    let entered;
    let readerAborted = false;
    let factories = 0;
    const started = new Promise((resolve) => { entered = resolve; });
    const cache = new EquipmentCache({ enabled: true }, 'ev_test', {
        credentialsReader: (_filename, { signal }) => new Promise((_resolve, reject) => {
            signal.addEventListener('abort', () => { readerAborted = true; reject(new Error('controlled read canceled')); });
            entered();
        }), clientFactory: () => { factories += 1; return new FakeClient(); }
    });
    t.after(() => cache.close());
    const controller = new AbortController();
    const lookup = cache.lookup('A', { signal: controller.signal });
    await started; controller.abort();
    assert.equal((await lookup).mode, 'BYPASS');
    assert.equal(readerAborted, true);
    assert.equal(factories, 0);
    assert.equal(cache.status().inFlight, 0);
    assert.equal(cache.status().ready, false);
});

test('reader忽略Signal并晚返回有效凭证，取消后也不能创建或连接client', async (t) => {
    let entered;
    let lateCredentials;
    let readSignal;
    let factories = 0;
    const started = new Promise((resolve) => { entered = resolve; });
    const cache = new EquipmentCache({ enabled: true }, 'ev_test', {
        credentialsReader: (_filename, { signal }) => new Promise((resolve) => {
            readSignal = signal; lateCredentials = resolve; entered(); // 故意不响应abort。
        }), clientFactory: () => { factories += 1; return new FakeClient(); }
    });
    t.after(() => cache.close());
    const controller = new AbortController();
    const lookup = cache.lookup('A', { signal: controller.signal });
    await started; controller.abort();
    assert.equal((await lookup).mode, 'BYPASS');
    assert.equal(readSignal.aborted, true);
    lateCredentials(credentials); await wait(0);
    assert.equal(factories, 0);
    assert.equal(cache.status().inFlight, 0);
    assert.equal(cache.status().ready, false);
    assert.equal(cache.credentials, null);
});

test('读取已完成但冷连接continuation尚未执行时取消，代际同样阻止新socket', async (t) => {
    let entered;
    let supply;
    let factories = 0;
    const started = new Promise((resolve) => { entered = resolve; });
    const cache = new EquipmentCache({ enabled: true }, 'ev_test', {
        credentialsReader: () => new Promise((resolve) => { supply = resolve; entered(); }),
        clientFactory: () => { factories += 1; return new FakeClient(); }
    });
    t.after(() => cache.close());
    const controller = new AbortController();
    const lookup = cache.lookup('A', { signal: controller.signal });
    await started;
    supply(credentials);
    queueMicrotask(() => controller.abort());
    assert.equal((await lookup).mode, 'BYPASS');
    await wait(0);
    assert.equal(factories, 0);
    assert.equal(cache.status().inFlight, 0);
    assert.equal(cache.status().ready, false);
});
