import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { setImmediate as nextTurn, setTimeout as delay } from 'node:timers/promises';
import { createApp } from '../src/server.mjs';

const token = 'controlled-resource-test-token';
const operationTimeoutMs = 250;
const model = {
    equipmentId: 'resource-model', equipmentName: '资源检查虚构设备',
    parts: [{ id: 'box', name: '底座', material: '钢', shape: 'box',
        dimensions: [20, 10, 5], position: [0, 0, 0] }]
};

// 只替换持久化边界；HTTP解析、鉴权、超时、取消、监听和关闭仍执行实际应用。
function controlledRepository() {
    let document;
    let closeCalls = 0;
    let activeAtClose;
    let getCalls = 0;
    const pending = new Map();
    const aborted = new Set();
    return {
        pending, aborted,
        get closeCalls() { return closeCalls; },
        get activeAtClose() { return activeAtClose; },
        get getCalls() { return getCalls; },
        initialize: async () => {},
        health: async () => {},
        list: async () => document ? [{ id: document.equipmentId, name: document.equipmentName,
            partCount: document.parts.length, revision: 1 }] : [],
        publish: async (value) => {
            document = structuredClone(value);
            return { id: value.equipmentId, name: value.equipmentName, partCount: value.parts.length, revision: 1 };
        },
        get: async (id, { signal }) => {
            getCalls += 1;
            if (id === 'error') throw new Error('controlled-internal-detail');
            if (!['timeout', 'cancel', 'closing'].includes(id))
                return id === document?.equipmentId ? structuredClone(document) : null;
            // 无自有计时器的受控任务只能由实际HTTP取消信号结束，不能靠替身自己到期通过。
            return new Promise((_resolve, reject) => {
                const abort = () => {
                    signal.removeEventListener('abort', abort);
                    pending.delete(id);
                    aborted.add(id);
                    reject(new Error('controlled-repository-abort'));
                };
                pending.set(id, abort);
                signal.addEventListener('abort', abort, { once: true });
                if (signal.aborted) abort();
            });
        },
        close: async () => {
            closeCalls += 1;
            // 不替应用取消任务，否则替身close会掩盖createApp漏取消的问题。
            activeAtClose = pending.size;
            document = undefined;
        }
    };
}

async function until(predicate, message) {
    const deadline = performance.now() + 3000;
    while (!predicate()) {
        assert.ok(performance.now() < deadline, message);
        await delay(10);
    }
}

function trackSocket(socket, sockets) {
    if (sockets.has(socket)) return;
    sockets.add(socket);
    socket.once('close', () => sockets.delete(socket));
}

// 使用内置HTTP客户端和专属keep-alive Agent，避免fetch全局连接池混入收尾观察。
function request(port, agent, sockets, route, { authorized = true, body } = {}) {
    let outgoing;
    const result = new Promise((resolve) => {
        outgoing = http.request({ host: '127.0.0.1', port, path: route, agent,
            method: body === undefined ? 'GET' : 'POST', headers: {
                ...(authorized ? { Authorization: `Bearer ${token}` } : {}),
                ...(body === undefined ? {} : { 'Content-Type': 'application/json' })
            } }, (incoming) => {
            const chunks = [];
            incoming.on('data', (chunk) => chunks.push(chunk));
            incoming.on('error', (error) => resolve({ error }));
            incoming.on('end', () => {
                try { resolve({ status: incoming.statusCode, json: JSON.parse(Buffer.concat(chunks).toString('utf8')) }); }
                catch (error) { resolve({ error }); }
            });
        });
        outgoing.on('socket', (socket) => trackSocket(socket, sockets));
        outgoing.on('error', (error) => resolve({ error }));
        outgoing.setTimeout(2000, () => outgoing.destroy(new Error('resource-test-client-deadline')));
        outgoing.end(body);
    });
    return { outgoing, result };
}

async function cycle() {
    const repository = controlledRepository();
    const app = await createApp({ token, operationTimeoutMs, closeTimeoutMs: 250,
        redis: { enabled: false } }, { repository });
    const serverSockets = new Set();
    const clientSockets = new Set();
    const agent = new http.Agent({ keepAlive: true, maxSockets: 4 });
    app.server.on('connection', (socket) => trackSocket(socket, serverSockets));
    let requests = 0;
    const call = (port, route, options) => {
        requests += 1;
        return request(port, agent, clientSockets, route, options);
    };
    try {
        const address = await app.start(0, '127.0.0.1');
        assert.equal(address.address, '127.0.0.1');
        assert.ok(address.port > 0);
        assert.equal(app.server.listening, true);
        const port = address.port;
        const published = await call(port, '/api/equipment', { body: JSON.stringify(model) }).result;
        assert.equal(published.status, 201);
        assert.equal(published.json.data.partCount, 1);
        // 3轮真实并发读：复用连接同时覆盖小模型、列表和鉴权拒绝。
        for (let round = 0; round < 3; round += 1) {
            const getCalls = repository.getCalls;
            const unauthorized = await call(port, '/api/equipment/resource-model', { authorized: false }).result;
            assert.equal(unauthorized.status, 401);
            assert.equal(repository.getCalls, getCalls);
            const [detail, list, health] = await Promise.all([
                call(port, '/api/equipment/resource-model').result,
                call(port, '/api/equipment').result,
                call(port, '/health').result
            ]);
            assert.equal(detail.status, 200);
            assert.deepEqual(detail.json.data, model);
            assert.equal(list.status, 200);
            assert.equal(list.json.data[0].id, model.equipmentId);
            assert.equal(health.status, 200);
        }
        const failed = await call(port, '/api/equipment/error').result;
        assert.equal(failed.status, 500);
        assert.equal(failed.json.error.code, 'INTERNAL_ERROR');
        assert.equal(JSON.stringify(failed.json).includes('controlled-internal-detail'), false);
        const timedOut = await call(port, '/api/equipment/timeout').result;
        assert.equal(timedOut.status, 503);
        assert.equal(timedOut.json.error.code, 'DATABASE_TIMEOUT');
        assert.ok(repository.aborted.has('timeout'));
        assert.equal(repository.pending.size, 0);

        const cancellationStarted = performance.now();
        const cancelled = call(port, '/api/equipment/cancel');
        await until(() => repository.pending.has('cancel'), '取消用请求没有进入实际仓储调用');
        assert.ok(serverSockets.size > 0 && clientSockets.size > 0);
        cancelled.outgoing.destroy(new Error('controlled-client-cancel'));
        assert.equal((await cancelled.result).error?.message, 'controlled-client-cancel');
        await until(() => repository.aborted.has('cancel'), '客户端断开没有取消实际应用的仓储调用');
        assert.ok(performance.now() - cancellationStarted < operationTimeoutMs,
            '客户端断开应在数据库操作时限到达前传播取消');
        assert.equal(repository.pending.size, 0);
        // 失败/超时/取消后仍可完整读取，防止仅验证资源归零而漏掉服务不可用。
        const recovered = await call(port, '/api/equipment/resource-model').result;
        assert.equal(recovered.status, 200);
        assert.deepEqual(recovered.json.data, model);

        const closing = call(port, '/api/equipment/closing');
        await until(() => repository.pending.has('closing'), '关闭用请求没有进入实际仓储调用');
        const started = performance.now();
        await app.close();
        assert.ok(performance.now() - started < 2000, '应用关闭超过受控检查预算');
        const closingResult = await closing.result;
        assert.ok(closingResult.error || closingResult.status === 503,
            '关闭必须结束等待中的真实HTTP请求');
        assert.ok(repository.aborted.has('closing'));
        await app.close();
        assert.equal(repository.closeCalls, 1, '应用重复关闭不得重复关闭仓储');
    } finally {
        await app.close();
        agent.destroy();
        await until(() => serverSockets.size === 0 && clientSockets.size === 0,
            '应用/客户端关闭后真实socket未回到零基线');
    }
    assert.equal(app.server.listening, false);
    const connections = await new Promise((resolve, reject) => app.server.getConnections((error, count) =>
        error ? reject(error) : resolve(count)));
    assert.equal(connections, 0);
    assert.equal(repository.pending.size, 0);
    assert.equal(repository.activeAtClose, 0, '应用必须先取消活动任务再关闭仓储');
    assert.equal(repository.closeCalls, 1);
    assert.equal(Object.keys(agent.sockets).length, 0);
    assert.equal(Object.keys(agent.freeSockets).length, 0);
    assert.equal(Object.keys(agent.requests).length, 0);
    return { requests, serverSockets: serverSockets.size, clientSockets: clientSockets.size,
        activeRepositoryTasks: repository.pending.size, listening: app.server.listening };
}

function counts(values) {
    const result = {};
    for (const value of values) result[value] = (result[value] ?? 0) + 1;
    return Object.fromEntries(Object.entries(result).sort(([a], [b]) => a.localeCompare(b)));
}

async function observe(label) {
    // 给socket close回调和GC各一个事件循环余量；不借用私有_getActiveHandles。
    for (let round = 0; round < 3; round += 1) { await nextTurn(); global.gc(); }
    await nextTurn();
    const memory = process.memoryUsage();
    const activeResources = process.getActiveResourcesInfo();
    const entries = process.report.getReport().libuv;
    assert.ok(Array.isArray(activeResources) && activeResources.every((value) => typeof value === 'string'));
    // libuv还包含loop记录；该记录无is_referenced，统计只依赖实际使用的type/is_active。
    assert.ok(Array.isArray(entries) && entries.every((value) => typeof value.type === 'string'
        && typeof value.is_active === 'boolean'),
    '诊断报告句柄形状未知，不能当作零句柄');
    const handles = entries.filter((value) => value.type !== 'loop');
    return { label, heapUsed: memory.heapUsed, rss: memory.rss, external: memory.external,
        activeResourceCounts: counts(activeResources),
        libuvHandleCounts: counts(handles.map((value) => value.type)),
        activeLibuvHandleCounts: counts(handles.filter((value) => value.is_active).map((value) => value.type)),
        // Node公开接口给libuv句柄，无法据此得到Windows全部进程句柄数。
        osProcessHandleCount: null };
}

test('有限HTTP资源检查：暖机、五批请求/应用重建，取消及关闭回到实际socket/任务基线',
    { timeout: 30000 }, async (t) => {
        assert.equal(typeof global.gc, 'function', '请使用 node --expose-gc --test test/resource.test.mjs');
        assert.equal(typeof process.getActiveResourcesInfo, 'function');
        assert.equal(typeof process.report?.getReport, 'function');
        for (let warmup = 0; warmup < 2; warmup += 1) await cycle();
        const baseline = await observe('warm-baseline');
        t.diagnostic(JSON.stringify(baseline));
        let totalRequests = 0;
        const observations = [];
        for (let batch = 1; batch <= 5; batch += 1) {
            let last;
            for (let index = 0; index < 3; index += 1) {
                last = await cycle();
                totalRequests += last.requests;
            }
            const observation = await observe(`batch-${batch}`);
            observations.push(observation);
            t.diagnostic(JSON.stringify({ ...observation, cycles: 3, batchRequests: 3 * last.requests, ...last }));
            // 已支持API返回数组中缺失的类型代表没有该类活动资源；API缺失/形状未知已先失败。
            for (const type of ['TCPSocketWrap', 'TCPServerWrap', 'TCPConnectWrap'])
                assert.equal(observation.activeResourceCounts[type] ?? 0, baseline.activeResourceCounts[type] ?? 0,
                    `${type} 未回到暖机后基线`);
            assert.deepEqual(observation.activeLibuvHandleCounts, baseline.activeLibuvHandleCounts,
                '活动libuv句柄未回到暖机后基线');
        }
        t.diagnostic(JSON.stringify({ measuredCycles: 15, warmupCycles: 2, totalRequests,
            heapUsedDelta: observations.at(-1).heapUsed - baseline.heapUsed,
            limitation: '有限受控仓储HTTP检查；堆增长只记录，不设任意字节门槛；不证明真实MySQL/Redis或零泄漏。' }));
    });
