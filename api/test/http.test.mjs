import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { createApp } from '../src/server.mjs';
import { ApiError, maximumBodyBytes } from '../src/validation.mjs';

function sample(id = 'A') {
    return { equipmentId: id, equipmentName: '中文', parts: [] };
}

test('纯HTTP：鉴权、成功信封、方法、JSON/类型/URL和body限制，不泄内部错误', async (t) => {
    const saved = new Map();
    let closed = false;
    const repository = {
        initialize: async () => {}, health: async () => {}, close: async () => { closed = true; },
        list: async () => [...saved.values()].map((value) => ({ id: value.equipmentId, name: value.equipmentName, partCount: 0, revision: 1 })),
        get: async (id) => saved.get(id),
        publish: async (value) => {
            if (value.equipmentId === 'internal') throw new Error('SQL password=secret');
            if (saved.has(value.equipmentId)) throw new ApiError(409, 'EQUIPMENT_EXISTS', '设备ID已存在。');
            saved.set(value.equipmentId, value);
            return { id: value.equipmentId, name: value.equipmentName, partCount: value.parts.length, revision: 1 };
        }
    };
    const app = await createApp({ token: 'pure-test-token' }, { repository });
    t.after(() => app.close());
    const address = await app.start(0);
    const base = `http://127.0.0.1:${address.port}`;
    const request = async (route, options = {}) => {
        const response = await fetch(base + route, { ...options, headers: { Authorization: 'Bearer pure-test-token', ...options.headers } });
        return { status: response.status, json: await response.json() };
    };
    assert.equal((await fetch(base + '/health')).status, 200);
    assert.equal((await fetch(base + '/api/equipment')).status, 401);
    assert.equal((await request('/api/equipment', { headers: { Authorization: 'Bearer wrong' } })).status, 401);
    assert.equal((await request('/api/equipment', { method: 'PUT' })).status, 405);
    assert.equal((await request('/health', { method: 'POST' })).status, 405);
    assert.equal((await request('/other')).status, 404);
    assert.equal((await request('/api/equipment/missing')).status, 404);
    assert.equal((await request('/api/equipment/%ZZ')).status, 400);
    assert.equal((await request('/api/equipment/' + 'a'.repeat(129))).status, 400);
    assert.equal((await request('/api/equipment', { method: 'POST', body: '{}' })).status, 415);
    const post = (body) => request('/api/equipment', { method: 'POST', headers: { 'Content-Type': 'application/json; charset=utf-8' }, body });
    assert.equal((await post('{bad')).status, 400);
    assert.equal((await post('[]')).status, 400);
    assert.equal((await post(Buffer.from([0x7b, 0xff, 0x7d]))).status, 400);
    assert.equal((await post(' '.repeat(maximumBodyBytes + 1))).status, 413);
    const id = '中文/部件? A ';
    assert.equal((await post(JSON.stringify(sample(id)))).status, 201);
    assert.deepEqual((await request('/api/equipment/' + encodeURIComponent(id))).json.data, sample(id));
    assert.equal((await post(JSON.stringify(sample(id)))).status, 409);
    assert.equal((await request('/api/equipment')).json.data[0].id, id);
    assert.equal((await post(JSON.stringify(sample('.')))).status, 201);
    const dot = await new Promise((resolve, reject) => {
        const req = http.get({ host: '127.0.0.1', port: address.port, path: '/api/equipment/%2e',
            headers: { Authorization: 'Bearer pure-test-token' } }, (res) => {
            let data = '';
            res.on('data', (chunk) => { data += chunk; });
            res.on('end', () => resolve(JSON.parse(data)));
        });
        req.on('error', reject);
    });
    assert.deepEqual(dot.data, sample('.'));
    for (const queryId of ['.', '..', '中文/斜线 ', 'plus+sign']) {
        if (queryId !== '.') assert.equal((await post(JSON.stringify(sample(queryId)))).status, 201);
        assert.deepEqual((await request('/api/equipment?id=' + encodeURIComponent(queryId))).json.data, sample(queryId));
    }
    assert.equal((await request('/api/equipment?id=%ZZ')).status, 400);
    assert.equal((await request('/api/equipment?id=%FF')).status, 400);
    assert.equal((await request('/api/equipment?id=')).status, 400);
    assert.equal((await request('/api/equipment?id=A&id=B')).status, 400);
    const error = await post(JSON.stringify(sample('internal')));
    assert.equal(error.status, 500);
    assert.equal(JSON.stringify(error.json).includes('secret'), false);

    // chunked发送证明上限来自实际流字节计数，而非仅Content-Length。
    const overflowStatus = await new Promise((resolve, reject) => {
        const req = http.request(base + '/api/equipment', { method: 'POST', headers: {
            Authorization: 'Bearer pure-test-token', 'Content-Type': 'application/json' } }, (res) => {
            res.resume(); res.on('end', () => resolve(res.statusCode));
        });
        req.on('error', reject);
        req.write(Buffer.alloc(maximumBodyBytes, 32));
        req.end('x');
    });
    assert.equal(overflowStatus, 413);
    await app.close();
    assert.equal(closed, true);
    assert.equal(app.server.listening, false);
});

test('HTTP数据库总时限独立于body时限，挂起health与close都可有界收尾', async () => {
    let aborted = false;
    const app = await createApp({ token: 'timeout-test', operationTimeoutMs: 50, closeTimeoutMs: 50 }, { repository: {
        initialize: async () => {}, close: () => new Promise(() => {}),
        health: ({ signal }) => new Promise(() => { signal.addEventListener('abort', () => { aborted = true; }, { once: true }); })
    } });
    const address = await app.start(0);
    const started = Date.now();
    const response = await fetch(`http://127.0.0.1:${address.port}/health`);
    assert.equal(response.status, 503);
    assert.equal((await response.json()).error.code, 'DATABASE_TIMEOUT');
    assert.ok(Date.now() - started < 500);
    assert.equal(aborted, true);
    const closing = Date.now();
    await app.close();
    assert.ok(Date.now() - closing < 500);
});
