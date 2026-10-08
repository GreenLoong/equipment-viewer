import http from 'node:http';
import { createHash, timingSafeEqual } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { TextDecoder } from 'node:util';
import { ApiError, maximumBodyBytes, validateEquipment, validateText } from './validation.mjs';
import { EquipmentRepository } from './repository.mjs';
import { validateAnnotationCreate, validateAnnotationUpdate, validateAnnotationQuery, validateUuid } from './annotations.mjs';
import { createEquipmentCache, normalizeCacheOptions } from './cache.mjs';

export const version = '0.8.0';
const hash = (value) => createHash('sha256').update(value).digest();

/** 读取忽略的本机配置；失败只给出安全提示，不输出秘密或文件内容。 */
export async function loadConfig(filename = process.env.EV_CONFIG_FILE
    ?? fileURLToPath(new URL('../runtime/service.local.json', import.meta.url))) {
    try {
        const config = JSON.parse(await readFile(filename, 'utf8'));
        if (!config || typeof config !== 'object' || Array.isArray(config)
            || typeof config.token !== 'string' || config.token.trim().length === 0
            || !config.mysql || typeof config.mysql.user !== 'string' || !config.mysql.user
            || typeof config.mysql.password !== 'string'
            || typeof config.mysql.database !== 'string' || !config.mysql.database)
            throw new Error('invalid config');
        const result = { ...config, host: config.host ?? '127.0.0.1', port: config.port ?? 8765,
            redis: normalizeCacheOptions(config.redis),
            operationTimeoutMs: config.operationTimeoutMs ?? 10000, closeTimeoutMs: config.closeTimeoutMs ?? 1000,
            mysql: { ...config.mysql, host: config.mysql.host ?? '127.0.0.1', port: config.mysql.port ?? 3310,
                connectionLimit: config.mysql.connectionLimit ?? 4 } };
        if (typeof result.host !== 'string' || !Number.isInteger(result.port) || result.port < 0 || result.port > 65535
            || typeof result.mysql.host !== 'string' || !Number.isInteger(result.mysql.port)
            || result.mysql.port < 1 || result.mysql.port > 65535
            || !Number.isInteger(result.mysql.connectionLimit) || result.mysql.connectionLimit < 1
            || result.mysql.connectionLimit > 32
            || !Number.isInteger(result.operationTimeoutMs) || result.operationTimeoutMs < 1 || result.operationTimeoutMs > 120000
            || !Number.isInteger(result.closeTimeoutMs) || result.closeTimeoutMs < 1 || result.closeTimeoutMs > 10000)
            throw new Error('invalid config');
        return result;
    } catch { throw new Error('本机API配置不可读或格式无效，请检查EV_CONFIG_FILE。'); }
}

function send(response, status, body, close = false, headers = {}) {
    if (response.destroyed || response.writableEnded) return;
    response.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8',
        'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff', ...(close ? { Connection: 'close' } : {}), ...headers });
    response.end(JSON.stringify(body));
}

/** 流式限制字节数；拒绝无效UTF-8，不让替换字符改变身份。 */
async function readJson(request) {
    if (Number(request.headers['content-length']) > maximumBodyBytes)
        throw new ApiError(413, 'BODY_TOO_LARGE', 'JSON超过5MiB上限。');
    return new Promise((resolve, reject) => {
        let chunks = [];
        let size = 0;
        let settled = false;
        const finish = (error, value) => {
            if (settled) return;
            settled = true;
            clearTimeout(timer);
            chunks = [];
            request.off('data', onData);
            request.off('end', onEnd);
            request.off('aborted', onAborted);
            // 保留error监听直到IncomingMessage回收；超限后客户端断开仍可能发出error。
            if (error) reject(error); else resolve(value);
        };
        const onData = (chunk) => {
            size += chunk.length;
            if (size > maximumBodyBytes) {
                finish(new ApiError(413, 'BODY_TOO_LARGE', 'JSON超过5MiB上限。'));
                request.resume();
            } else chunks.push(chunk);
        };
        const onEnd = () => {
            try {
                const text = new TextDecoder('utf-8', { fatal: true }).decode(Buffer.concat(chunks));
                finish(null, JSON.parse(text));
            } catch { finish(new ApiError(400, 'INVALID_JSON', '请求体必须是有效UTF-8 JSON。')); }
        };
        const onAborted = () => finish(new ApiError(400, 'REQUEST_ABORTED', '请求在读取完成前中断。'));
        const timer = setTimeout(() => {
            finish(new ApiError(408, 'REQUEST_TIMEOUT', '读取请求体超时。'));
            request.resume();
        }, 15000);
        timer.unref();
        request.on('data', onData);
        request.on('end', onEnd);
        request.on('aborted', onAborted);
        request.on('error', onAborted);
    });
}

/** 创建可关闭的HTTP应用；可传入隔离Repository用于纯HTTP验证。 */
export async function createApp(config, { repository, cache, cacheDependencies } = {}) {
    const cacheOptions = normalizeCacheOptions(config.redis);
    const operationTimeoutMs = config.operationTimeoutMs ?? 10000;
    const closeTimeoutMs = config.closeTimeoutMs ?? 1000;
    const store = repository ?? new EquipmentRepository(config.mysql, { operationTimeoutMs, closeTimeoutMs });
    const operations = new Set();
    let equipmentDetailReadAttempts = 0;
    const databaseOperation = (method, args = [], signal) => new Promise((resolve, reject) => {
        const controller = new AbortController();
        let settled = false;
        const finish = (error, value) => {
            if (settled) return;
            settled = true;
            clearTimeout(timer);
            operations.delete(cancel);
            signal?.removeEventListener('abort', cancel);
            if (error) reject(error); else resolve(value);
        };
        const cancel = () => {
            controller.abort(); // Repository收到取消会destroy真实连接，晚到连接也被销毁。
            finish(new ApiError(503, 'DATABASE_TIMEOUT', '共享数据库操作超时，请稍后重试。'));
        };
        const timer = setTimeout(cancel, operationTimeoutMs);
        operations.add(cancel);
        signal?.addEventListener('abort', cancel, { once: true });
        if (signal?.aborted) { cancel(); return; }
        Promise.resolve().then(() => {
            if (controller.signal.aborted) throw new ApiError(503, 'DATABASE_TIMEOUT', '共享数据库操作超时，请稍后重试。');
            if (method === 'get') equipmentDetailReadAttempts += 1;
            return store[method](...args, { signal: controller.signal });
        })
            .then((value) => finish(null, value), (error) => finish(error));
    });
    const closeStore = () => new Promise((resolve) => {
        // Repository自身先到期并强制销毁socket；外围看门狗留一个事件循环余量。
        const timer = setTimeout(resolve, closeTimeoutMs + 50);
        Promise.resolve().then(() => store.close()).then(() => { clearTimeout(timer); resolve(); },
            () => { clearTimeout(timer); resolve(); });
    });
    try { await databaseOperation('initialize'); } catch (error) { await closeStore(); throw error; }
    let equipmentCache;
    try { equipmentCache = cache ?? await createEquipmentCache(cacheOptions, config.mysql?.database ?? 'injected-test', cacheDependencies); }
    catch (error) { await closeStore(); throw error; }
    const tokenHash = hash(config.token);
    const server = http.createServer(async (request, response) => {
        const requestAbort = new AbortController();
        response.once('close', () => { if (!response.writableEnded) requestAbort.abort(); });
        const runDatabase = (method, ...args) => databaseOperation(method, args, requestAbort.signal);
        const sendEquipment = async (id) => {
            // 调用处已鉴权并验证ID，非法请求不连接Redis、不改变缓存计数。
            const cached = await equipmentCache.lookup(id, { signal: requestAbort.signal });
            const equipment = cached.document ?? await runDatabase('get', id);
            if (!equipment) throw new ApiError(404, 'NOT_FOUND', '设备不存在。');
            if (cached.mode === 'MISS') await equipmentCache.fill(id, equipment, { signal: requestAbort.signal });
            send(response, 200, { data: equipment }, false, { 'X-Cache': cached.mode });
        };
        try {
            // WHATWG URL会归一化点路径，破坏合法设备ID "."/".." 的原文身份。
            if (typeof request.url !== 'string' || !request.url.startsWith('/'))
                throw new ApiError(400, 'INVALID_PATH', '请求路径无效。');
            const pathname = request.url.split('?', 1)[0];
            const query = request.url.includes('?') ? request.url.slice(request.url.indexOf('?') + 1) : '';
            if (pathname !== '/health') {
                const authorization = request.headers.authorization;
                if (typeof authorization !== 'string' || !authorization.startsWith('Bearer ')
                    || !timingSafeEqual(hash(authorization.slice(7)), tokenHash))
                    throw new ApiError(401, 'UNAUTHORIZED', '访问令牌无效。');
            }
            if (pathname === '/health') {
                if (request.method !== 'GET') throw new ApiError(405, 'METHOD_NOT_ALLOWED', '此接口只允许GET。');
                await runDatabase('health');
                send(response, 200, { data: { service: 'equipment-viewer', version, mysql: 'up' } });
            } else if (pathname === '/api/status') {
                if (request.method !== 'GET') throw new ApiError(405, 'METHOD_NOT_ALLOWED', '此接口只允许GET。');
                let ready = true;
                try { await runDatabase('health'); } catch { ready = false; }
                send(response, 200, { data: { service: 'equipment-viewer', version,
                    mysql: { ready, equipmentDetailReadAttempts: Number.isSafeInteger(store.equipmentDetailReadAttempts)
                        ? store.equipmentDetailReadAttempts : equipmentDetailReadAttempts }, cache: equipmentCache.status() } });
            } else if (pathname === '/api/equipment') {
                if (request.method === 'GET') {
                    const parameters = new URLSearchParams(query);
                    if (parameters.has('id')) {
                        if (parameters.getAll('id').length !== 1)
                            throw new ApiError(400, 'INVALID_ID', '只允许一个设备ID查询参数。');
                        // URLSearchParams宽容替换无效百分号/UTF-8，额外严格decode防止身份变化。
                        const raw = query.split('&').find((item) => {
                            try { return decodeURIComponent(item.split('=', 1)[0].replace(/\+/g, ' ')) === 'id'; }
                            catch { return false; }
                        });
                        let id;
                        try { id = decodeURIComponent((raw.includes('=') ? raw.slice(raw.indexOf('=') + 1) : '').replace(/\+/g, ' ')); }
                        catch { throw new ApiError(400, 'INVALID_ID', '设备ID的URL编码无效。'); }
                        validateText(id, 'equipmentId', true);
                        await sendEquipment(id);
                    } else send(response, 200, { data: await runDatabase('list') });
                }
                else if (request.method === 'POST') {
                    if (!/^application\/json(?:\s*;.*)?$/i.test(request.headers['content-type'] ?? ''))
                        throw new ApiError(415, 'UNSUPPORTED_MEDIA_TYPE', '发布设备需要application/json。');
                    const equipment = validateEquipment(await readJson(request));
                    send(response, 201, { data: await runDatabase('publish', equipment) });
                } else throw new ApiError(405, 'METHOD_NOT_ALLOWED', '此接口只允许GET或POST。');
            } else if (pathname.startsWith('/api/equipment/') && !pathname.slice(15).includes('/')) {
                if (request.method !== 'GET') throw new ApiError(405, 'METHOD_NOT_ALLOWED', '此接口只允许GET。');
                let id;
                try { id = decodeURIComponent(pathname.slice(15)); }
                catch { throw new ApiError(400, 'INVALID_ID', '设备ID的URL编码无效。'); }
                validateText(id, 'equipmentId', true);
                await sendEquipment(id);
            } else if (pathname === '/api/annotations') {
                if (request.method === 'GET') {
                    send(response, 200, { data: await runDatabase('listAnnotations', validateAnnotationQuery(query)) });
                } else if (request.method === 'POST') {
                    if (!/^application\/json(?:\s*;.*)?$/i.test(request.headers['content-type'] ?? ''))
                        throw new ApiError(415, 'UNSUPPORTED_MEDIA_TYPE', '批注请求需要application/json。');
                    const result = await runDatabase('createAnnotation', validateAnnotationCreate(await readJson(request)));
                    send(response, result.created ? 201 : 200, { data: result.annotation });
                } else throw new ApiError(405, 'METHOD_NOT_ALLOWED', '此接口只允许GET或POST。');
            } else if (pathname.startsWith('/api/annotations/') && !pathname.slice(17).includes('/')) {
                if (request.method !== 'PUT') throw new ApiError(405, 'METHOD_NOT_ALLOWED', '此接口只允许PUT。');
                if (!/^application\/json(?:\s*;.*)?$/i.test(request.headers['content-type'] ?? ''))
                    throw new ApiError(415, 'UNSUPPORTED_MEDIA_TYPE', '批注请求需要application/json。');
                let id;
                try { id = validateUuid(decodeURIComponent(pathname.slice(17))); }
                catch { throw new ApiError(400, 'VALIDATION_ERROR', '批注ID必须是UUID。'); }
                send(response, 200, { data: await runDatabase('updateAnnotation', id, validateAnnotationUpdate(await readJson(request))) });
            } else throw new ApiError(404, 'NOT_FOUND', '接口不存在。');
        } catch (error) {
            const safe = error instanceof ApiError ? error : new ApiError(500, 'INTERNAL_ERROR', '服务暂时无法完成请求。');
            // 限制后停止缓存并排空流，HTTP请求总时限约束排空；立即关socket会使正在上传的客户端丢失413。
            send(response, safe.status, { error: { code: safe.code, message: safe.message } }, safe.status === 408);
            request.resume();
        }
    });
    server.requestTimeout = 20000;
    server.headersTimeout = 10000;
    server.keepAliveTimeout = 5000;
    server.maxRequestsPerSocket = 100;
    server.on('clientError', (_error, socket) => {
        if (socket.writable) socket.end('HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n');
        else socket.destroy();
    });
    let closed = false;
    let closing;
    return {
        server,
        async start(port = config.port ?? 8765, host = config.host ?? '127.0.0.1') {
            await new Promise((resolve, reject) => {
                server.once('error', reject);
                server.listen(port, host, () => { server.off('error', reject); resolve(); });
            });
            return server.address();
        },
        async close() {
            if (closed) return closing;
            closed = true;
            for (const cancel of [...operations]) cancel();
            closing = (async () => {
                if (server.listening) {
                    await new Promise((resolve) => {
                        const timer = setTimeout(resolve, closeTimeoutMs);
                        server.close(() => { clearTimeout(timer); resolve(); });
                        server.closeAllConnections();
                    });
                }
                try { await equipmentCache.close(); }
                finally { await closeStore(); }
            })();
            return closing;
        }
    };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    let app;
    try {
        app = await createApp(await loadConfig());
        const address = await app.start();
        console.log(`equipment-viewer ${version} listening at ${address.address}:${address.port}`);
        for (const signal of ['SIGINT', 'SIGTERM']) process.once(signal, async () => { await app.close(); });
    } catch {
        if (app) await app.close();
        console.error('API启动失败，请检查本机配置、独立MySQL连接与数据库结构。');
        process.exitCode = 1;
    }
}
