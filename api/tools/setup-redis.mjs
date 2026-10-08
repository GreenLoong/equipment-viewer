import { randomBytes } from 'node:crypto';
import { mkdir, open, readFile, unlink } from 'node:fs/promises';
import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const redisPurpose = 'equipment-viewer-simulation-redis-v1';
export const linuxDirectory = '/opt/equipment-viewer-redis/runtime-6380';
export const redisConfigFile = fileURLToPath(new URL('../runtime/redis.local.json', import.meta.url));
const pythonFile = fileURLToPath(new URL('./redis-stdio.py', import.meta.url));

/** Windows绝对路径映射到已有WSL挂载，路径作为独立argv，秘密不进参数。 */
export function toWslPath(filename) {
    const match = /^([a-z]):[\\/](.*)$/i.exec(filename);
    if (!match) throw new Error('Windows absolute path required');
    return `/mnt/${match[1].toLowerCase()}/${match[2].replace(/\\/g, '/')}`;
}

/** 核对持久目的标记和专属端点；不改service/test配置或启用API缓存。 */
export function validateRedisConfig(config) {
    if (!config || config.purpose !== redisPurpose || config.host !== '127.0.0.1' || config.port !== 6380
        || config.version !== '7.4.7' || config.linuxDirectory !== linuxDirectory
        || typeof config.password !== 'string' || !/^[a-f0-9]{64}$/.test(config.password)
        || typeof config.distribution !== 'string' || !/^[a-z0-9._-]{1,64}$/i.test(config.distribution))
        throw new Error('Redis private config mismatch');
    return config;
}

export async function loadRedisConfig(filename = redisConfigFile) {
    return validateRedisConfig(JSON.parse(await readFile(filename, 'utf8')));
}

/** stdin只传一次小型JSON；stdout仅接收安全状态，stderr丢弃且子进程有总时限。 */
export function runWslSetup(config, operation = 'setup', { spawnProcess = spawn, timeoutMs = 30000 } = {}) {
    return new Promise((resolve, reject) => {
        let output = '';
        let complete = false;
        const child = spawnProcess('wsl.exe', ['--distribution', config.distribution, '--user', 'root', '--exec',
            'python3', toWslPath(pythonFile), '--setup'], { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
        const finish = (error) => {
            if (complete) return;
            complete = true;
            clearTimeout(timer);
            if (error) {
                child.stdin.destroy();
                child.stdout.destroy();
                child.stderr.destroy();
                child.kill();
                reject(new Error('Project WSL Redis operation failed'));
                return;
            }
            try {
                const result = JSON.parse(output.trim());
                if (result.port !== 6380 || !['ready', 'stopped', 'not-listening'].includes(result.state)) throw new Error('invalid result');
                resolve(result);
            } catch { reject(new Error('Project WSL Redis result invalid')); }
        };
        const timer = setTimeout(() => finish(new Error('deadline')), timeoutMs);
        child.stdout.on('data', (chunk) => {
            output += chunk.toString('utf8');
            if (Buffer.byteLength(output) > 32768) finish(new Error('output limit'));
        });
        child.stderr.resume();
        child.stdin.on('error', () => finish(new Error('stdin failure')));
        child.stdout.on('error', () => finish(new Error('stdout failure')));
        child.stderr.on('error', () => finish(new Error('stderr failure')));
        child.once('error', () => finish(new Error('spawn failure')));
        child.once('close', (code) => finish(code === 0 ? null : new Error('child failure')));
        child.stdin.end(JSON.stringify({ ...config, operation }));
    });
}

async function main() {
    if (process.platform !== 'win32') throw new Error('Windows only');
    const operation = process.argv[2] ?? 'setup';
    if (!['setup', 'stop'].includes(operation)) throw new Error('invalid operation');
    let config;
    try { config = await loadRedisConfig(); }
    catch (error) {
        if (error.code !== 'ENOENT' || operation === 'stop') throw error;
        config = validateRedisConfig({ purpose: redisPurpose, host: '127.0.0.1', port: 6380, version: '7.4.7',
            linuxDirectory, distribution: process.env.EV_REDIS_WSL_DISTRO ?? 'Ubuntu-24.04', password: randomBytes(32).toString('hex') });
        await mkdir(path.dirname(redisConfigFile), { recursive: true });
        let handle;
        let owned = false;
        let written = false;
        try {
            handle = await open(redisConfigFile, 'wx', 0o600);
            owned = true;
            await handle.writeFile(JSON.stringify(config, null, 4) + '\n', { encoding: 'utf8' });
            await handle.close();
            handle = undefined;
            written = true;
        } finally {
            try { if (handle) await handle.close(); }
            finally { if (owned && !written) await unlink(redisConfigFile); }
        }
    }
    const result = await runWslSetup(config, operation);
    console.log(`项目Redis环境状态=${result.state} port=6380；未启用API缓存，未更改WSL全局网络配置。`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main().catch(() => {
    console.error('项目Redis环境操作失败；未知端口/PID未操作，配置及目录保留，请主管检查专属状态。');
    process.exitCode = 1;
});
