import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { TextDecoder } from 'node:util';
import { createClient, RESP_TYPES } from '@redis/client';
import { validateEquipment, validateText } from './validation.mjs';

const apiDirectory = fileURLToPath(new URL('../', import.meta.url));
const defaults = { enabled: false, configFile: 'runtime/redis.local.json', namespace: 'geometry-v1', ttlSeconds: 60,
    connectTimeoutMs: 500, commandTimeoutMs: 500, reconnectDelayMs: 1000, maxPendingCommands: 8,
    maxValueBytes: 6 * 1024 * 1024, closeTimeoutMs: 500 };

/** 配置结构错误拒绝启动；缺少可选redis对象保持完全禁用。 */
export function normalizeCacheOptions(value) {
    if (value !== undefined && (value === null || typeof value !== 'object' || Array.isArray(value)))
        throw new Error('Invalid cache configuration');
    if (Object.keys(value ?? {}).some((key) => !Object.hasOwn(defaults, key))) throw new Error('Invalid cache configuration');
    const result = { ...defaults, ...value };
    const integer = (key, minimum, maximum) => Number.isInteger(result[key]) && result[key] >= minimum && result[key] <= maximum;
    if (typeof result.enabled !== 'boolean' || typeof result.configFile !== 'string' || result.configFile.trim().length === 0
        || typeof result.namespace !== 'string' || !/^[A-Za-z0-9_-]{1,64}$/.test(result.namespace)
        || !integer('ttlSeconds', 1, 3600) || !integer('connectTimeoutMs', 50, 5000)
        || !integer('commandTimeoutMs', 50, 5000) || !integer('closeTimeoutMs', 50, 5000)
        || !integer('reconnectDelayMs', 100, 10000) || !integer('maxPendingCommands', 1, 32)
        || !integer('maxValueBytes', 1, 6 * 1024 * 1024)) throw new Error('Invalid cache configuration');
    return result;
}

/** 固定前缀/schema2/库/namespace加原文UTF8 ID摘要，避免身份归一化或测试污染。 */
export function equipmentCacheKey(database, namespace, id) {
    validateText(id, 'equipmentId', true);
    return `ev:geometry:schema2:${encodeURIComponent(database)}:${namespace}:`
        + createHash('sha256').update(Buffer.from(id, 'utf8')).digest('hex');
}

// TYPE/STRLEN/GET在同一脚本中，巨型值不进入Node RESP parser，也没有长度检查后替换竞态。
export const boundedGetScript = `local kind=redis.call('TYPE',KEYS[1]).ok
if kind=='none' then return {0} end
if kind~='string' or redis.call('STRLEN',KEYS[1])>tonumber(ARGV[1]) then
  redis.call('DEL',KEYS[1]); return {2}
end
return {1,redis.call('GET',KEYS[1])}`;

class CacheFailure extends Error {
    constructor(reason) { super('Cache unavailable'); this.reason = reason; }
}

async function readCredentials(filename, { signal } = {}) {
    const value = JSON.parse(await readFile(filename, { encoding: 'utf8', signal }));
    if (value.purpose !== 'equipment-viewer-simulation-redis-v1' || value.host !== '127.0.0.1' || value.port !== 6380
        || typeof value.password !== 'string' || !/^[a-f0-9]{64}$/.test(value.password)) throw new Error('Cache credentials unavailable');
    return { host: value.host, port: value.port, password: value.password };
}

/** 可重建设备缓存；秘密/原始错误/key不进入诊断或日志，批注调用方不使用本类。 */
export class EquipmentCache {
    constructor(options, database, { clientFactory = createClient, credentialsReader = readCredentials,
        now = () => Date.now() } = {}) {
        this.options = normalizeCacheOptions(options);
        this.database = database;
        this.clientFactory = clientFactory;
        this.credentialsReader = credentialsReader;
        this.now = now;
        this.client = null;
        this.connecting = null;
        this.cancelConnect = null;
        this.cancelCredentials = null;
        this.connectionGeneration = 0;
        this.credentials = null;
        this.nextReconnect = 0;
        this.pending = new Set();
        this.closed = false;
        this.closing = null;
        this.counters = { hits: 0, misses: 0, bypass: 0, invalidValues: 0, commandErrors: 0, sets: 0 };
        this.lastFailure = null;
    }

    /** 启用后秘密文件不可读仅降级；禁用时完全不读取文件或创建连接。 */
    async initialize() {
        if (!this.options.enabled) return;
        this.credentials = await this.loadCredentials();
    }

    loadCredentials() {
        return new Promise((resolve) => {
            const controller = new AbortController();
            let settled = false;
            const finish = (value) => {
                if (settled) return;
                settled = true;
                clearTimeout(timer);
                if (this.cancelCredentials === cancel) this.cancelCredentials = null;
                if (!value) {
                    this.lastFailure = 'config_unavailable';
                    this.nextReconnect = this.now() + this.options.reconnectDelayMs;
                }
                resolve(value);
            };
            const cancel = () => { controller.abort(); finish(null); };
            const timer = setTimeout(cancel, this.options.connectTimeoutMs);
            this.cancelCredentials = cancel;
            Promise.resolve().then(() => this.credentialsReader(path.resolve(apiDirectory, this.options.configFile), { signal: controller.signal }))
                .then((value) => finish(this.closed ? null : value), () => finish(null));
        });
    }

    invalidate(reason) {
        const client = this.client;
        this.client = null;
        this.connectionGeneration += 1;
        this.lastFailure = reason;
        this.nextReconnect = this.now() + this.options.reconnectDelayMs;
        client?.destroy(); // 固定6.3.0 destroy清命令队列并销毁真实socket；不等待无界drain。
    }

    async ensureConnection() {
        if (this.closed || !this.options.enabled) return false;
        if (this.client?.isReady) return true;
        if (this.connecting) return this.connecting;
        if (this.now() < this.nextReconnect) return false;
        const generation = this.connectionGeneration;
        this.connecting = (async () => {
            const credentials = this.credentials ?? await this.loadCredentials();
            // 读取刚完成、await continuation尚未执行时也可能取消；代际阻止迟到结果建新socket。
            if (!credentials || this.closed || generation !== this.connectionGeneration) return false;
            this.credentials = credentials;
            const client = this.clientFactory({ socket: { host: this.credentials.host, port: this.credentials.port,
                connectTimeout: this.options.connectTimeoutMs, reconnectStrategy: false },
                password: this.credentials.password, RESP: 2, disableOfflineQueue: true,
                commandsQueueMaxLength: this.options.maxPendingCommands, disableClientInfo: true });
            this.client = client;
            client.on('error', () => {
                if (this.client === client) this.invalidate('connection_error');
            });
            return new Promise((resolve) => {
                let settled = false;
                const finish = (result) => {
                    if (settled) return;
                    settled = true;
                    clearTimeout(timer);
                    if (this.cancelConnect === cancel) this.cancelConnect = null;
                    resolve(result);
                };
                const cancel = (reason = 'connection_error') => {
                    if (settled) return;
                    this.invalidate(reason);
                    finish(false);
                };
                const timer = setTimeout(() => cancel('connect_timeout'), this.options.connectTimeoutMs);
                this.cancelConnect = cancel;
                Promise.resolve().then(() => {
                    if (settled || this.closed || this.client !== client) throw new CacheFailure('connection_error');
                    return client.connect();
                }).then(() => {
                    if (settled || this.closed || this.client !== client) { client.destroy(); finish(false); return; }
                    this.lastFailure = null;
                    finish(true);
                }, () => cancel('connection_error'));
            });
        })().catch(() => { this.invalidate('connection_error'); return false; }).finally(() => { this.connecting = null; });
        return this.connecting;
    }

    /** 排队+连接+已发送命令都有有界收尾；超时/取消实际destroy，禁offline queue。 */
    command(arguments_, { signal } = {}) {
        if (this.closed) return Promise.reject(new CacheFailure('connection_error'));
        if (this.pending.size >= this.options.maxPendingCommands) return Promise.reject(new CacheFailure('capacity'));
        return new Promise((resolve, reject) => {
            let settled = false;
            let timer;
            let candidate;
            const finish = (error, value) => {
                if (settled) return;
                settled = true;
                clearTimeout(timer);
                signal?.removeEventListener('abort', abort);
                this.pending.delete(abort);
                if (error) reject(error); else resolve(value);
            };
            const abort = () => {
                if (settled) return;
                this.cancelCredentials?.();
                this.cancelConnect?.('connection_error');
                this.invalidate('connection_error');
                finish(new CacheFailure('connection_error'));
            };
            this.pending.add(abort);
            signal?.addEventListener('abort', abort, { once: true });
            if (signal?.aborted) { abort(); return; }
            (async () => {
                try {
                    if (!await this.ensureConnection()) throw new CacheFailure(this.lastFailure ?? 'connection_error');
                    if (settled || this.closed) return;
                    const client = this.client;
                    candidate = client;
                    timer = setTimeout(() => {
                        if (this.client === client) this.invalidate('command_timeout');
                        else client.destroy(); // 旧命令的晚到截止不能销毁后来已恢复的新连接。
                        finish(new CacheFailure('command_timeout'));
                    }, this.options.commandTimeoutMs);
                    const result = await client.withTypeMapping({ [RESP_TYPES.BLOB_STRING]: Buffer }).sendCommand(arguments_);
                    finish(null, result);
                } catch (error) {
                    if (settled) return;
                    if (!(error instanceof CacheFailure) && this.client === candidate)
                        this.invalidate(this.lastFailure === 'command_timeout' ? 'command_timeout' : 'connection_error');
                    finish(error instanceof CacheFailure ? error : new CacheFailure(this.lastFailure ?? 'connection_error'));
                }
            })();
        });
    }

    recordFailure(error) {
        this.lastFailure = error instanceof CacheFailure ? error.reason : 'connection_error';
        if (!['capacity', 'config_unavailable'].includes(this.lastFailure)) this.counters.commandErrors += 1;
    }

    /** 一个合法详情lookup只计HIT/MISS/BYPASS之一；非法ID在计数前拒绝。 */
    async lookup(id, options) {
        const key = equipmentCacheKey(this.database, this.options.namespace, id);
        if (!this.options.enabled) { this.counters.bypass += 1; return { mode: 'BYPASS', document: null }; }
        try {
            const reply = await this.command(['EVAL', boundedGetScript, '1', key, String(this.options.maxValueBytes)], options);
            if (!Array.isArray(reply) || ![0, 1, 2].includes(reply[0])) throw new CacheFailure('connection_error');
            if (reply[0] === 1) {
                try {
                    const bytes = Buffer.isBuffer(reply[1]) ? reply[1] : Buffer.from(reply[1]);
                    if (bytes.length > this.options.maxValueBytes) throw new Error('value limit');
                    const value = validateEquipment(JSON.parse(new TextDecoder('utf8', { fatal: true }).decode(bytes)));
                    if (value.equipmentId !== id) throw new Error('identity mismatch');
                    this.counters.hits += 1;
                    return { mode: 'HIT', document: value };
                } catch {
                    this.counters.invalidValues += 1;
                    this.lastFailure = 'invalid_value';
                    try { await this.command(['DEL', key], options); } catch (error) { this.recordFailure(error); }
                }
            } else if (reply[0] === 2) {
                this.counters.invalidValues += 1;
                this.lastFailure = 'invalid_value';
            }
            this.counters.misses += 1;
            return { mode: 'MISS', document: null };
        } catch (error) {
            if (error instanceof CacheFailure && error.reason === 'connection_error' && this.client?.isReady)
                this.invalidate('connection_error');
            this.recordFailure(error);
            this.counters.bypass += 1;
            return { mode: 'BYPASS', document: null };
        }
    }

    /** MISS才best-effort SET EX，失败只计诊断，不改变已读取MySQL的成功结果。 */
    async fill(id, document, options) {
        if (!this.options.enabled || this.closed) return false;
        try {
            const value = validateEquipment(document);
            if (value.equipmentId !== id) return false;
            const json = JSON.stringify(value);
            if (Buffer.byteLength(json) > this.options.maxValueBytes) return false;
            const reply = await this.command(['SET', equipmentCacheKey(this.database, this.options.namespace, id), json,
                'EX', String(this.options.ttlSeconds)], options);
            if (reply !== 'OK') throw new CacheFailure('connection_error');
            this.counters.sets += 1;
            return true;
        } catch (error) { this.recordFailure(error); return false; }
    }

    /** 只暴露本进程计数及安全枚举，不含密钥/命名空间/路径/原始错误。 */
    status() {
        const ready = !this.closed && Boolean(this.client?.isReady);
        return { enabled: this.options.enabled, ready, state: !this.options.enabled ? 'disabled' : ready ? 'ready' : 'degraded',
            ...this.counters, inFlight: this.pending.size, lastFailure: this.lastFailure };
    }

    /** 缓存为可重建数据，关闭直接取消队列/destroy而不等无界命令drain。 */
    async close() {
        if (this.closing) return this.closing;
        if (this.closed) return;
        this.closed = true;
        this.cancelCredentials?.();
        for (const abort of [...this.pending]) abort();
        this.cancelConnect?.('connection_error');
        const client = this.client;
        this.client = null;
        client?.destroy();
        this.closing = new Promise((resolve) => {
            const finish = () => { clearTimeout(timer); resolve(); };
            const timer = setTimeout(() => { client?.destroy(); finish(); }, this.options.closeTimeoutMs);
            Promise.resolve(this.connecting).then(finish, finish);
        });
        return this.closing;
    }
}

export async function createEquipmentCache(options, database, dependencies) {
    const cache = new EquipmentCache(options, database, dependencies);
    await cache.initialize();
    return cache;
}
