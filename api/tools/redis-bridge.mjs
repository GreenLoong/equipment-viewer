import net from 'node:net';
import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadRedisConfig, toWslPath, validateRedisConfig } from './setup-redis.mjs';

const pythonFile = fileURLToPath(new URL('./redis-stdio.py', import.meta.url));

/** 前台TCP桥，每连接一个受控WSL stdio子进程，pipe自动背压，无离线/无界队列。 */
export function createRedisBridge(config, { spawnProcess = spawn, idleTimeoutMs = 30000,
    childCloseTimeoutMs = 1500, killGraceMs = 500, maximumConnections = 8 } = {}) {
    validateRedisConfig(config);
    const sessions = new Set();
    let total = 0;
    let closed = false;
    let closing;
    const server = net.createServer({ allowHalfOpen: true }, (socket) => {
        if (closed || sessions.size >= maximumConnections) { socket.destroy(); return; }
        let child;
        try {
            child = spawnProcess('wsl.exe', ['--distribution', config.distribution, '--user', 'root', '--exec',
                'python3', toWslPath(pythonFile), '127.0.0.1', '6380'],
                { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
        } catch { socket.destroy(); return; }
        let stopping = false;
        let childClosed = false;
        let waiterSettled = false;
        let killTimer;
        let confirmTimer;
        let eofTimer;
        let settle;
        const finished = new Promise((resolve) => { settle = resolve; });
        const session = { socket, child, finished, stop: null, unconfirmed: false };
        sessions.add(session);
        total += 1;
        const clearPipes = () => {
            socket.unpipe(child.stdin);
            child.stdout.unpipe(socket);
            child.stdin.destroy();
            child.stdout.destroy();
            child.stderr.destroy();
        };
        const complete = (confirmed) => {
            if (childClosed) return;
            clearTimeout(killTimer);
            clearTimeout(confirmTimer);
            clearTimeout(eofTimer);
            clearPipes();
            socket.destroy();
            if (confirmed) {
                childClosed = true;
                sessions.delete(session);
            } else {
                // kill请求不是退出证据：持续计入预算，直到真正ChildProcess close才移除。
                session.unconfirmed = true;
            }
            if (!waiterSettled) { waiterSettled = true; settle(confirmed); }
        };
        const stop = () => {
            if (stopping || childClosed) return;
            stopping = true;
            clearTimeout(eofTimer);
            socket.unpipe(child.stdin);
            child.stdout.unpipe(socket);
            socket.destroy();
            child.stdin.end(); // Python看到EOF后有1秒排空预算并关闭Linux socket。
            child.stdout.destroy();
            child.stderr.destroy();
            killTimer = setTimeout(() => {
                child.kill(); // 只终止此连接由本桥创建的wsl.exe，不操作Redis PID。
                confirmTimer = setTimeout(() => {
                    child.unref?.();
                    complete(false); // 有界返回但明确未确认退出，不伪造零残留。
                }, killGraceMs);
            }, childCloseTimeoutMs);
        };
        session.stop = stop;
        socket.setTimeout(idleTimeoutMs, stop);
        socket.on('error', stop);
        socket.once('close', stop);
        socket.once('end', () => {
            // allowHalfOpen保留最后RESP排空，但EOF子进程也必须在有界预算结束。
            eofTimer = setTimeout(stop, childCloseTimeoutMs);
        });
        child.stdin.on('error', stop);
        child.stdout.on('error', stop);
        child.stderr.on('error', stop);
        child.once('error', stop);
        child.once('close', () => complete(true));
        // 两侧pipe遵循write(false)/drain；每侧stream的高水位+Python两64KiB队列限制缓冲。
        socket.pipe(child.stdin);
        child.stdout.pipe(socket);
        child.stderr.resume(); // stderr绝不写入RESP，也不打印密码/请求字节。
    });
    return {
        server,
        diagnostics: () => ({ activeConnections: sessions.size, totalConnections: total,
            unconfirmedChildren: [...sessions].filter((session) => session.unconfirmed).length }),
        async start(port = 6380, host = '127.0.0.1') {
            if (closed) throw new Error('bridge already closed');
            if (host !== '127.0.0.1') throw new Error('loopback only');
            await new Promise((resolve, reject) => {
                server.once('error', reject);
                server.listen(port, host, () => { server.off('error', reject); resolve(); });
            });
            return server.address();
        },
        async close() {
            if (closed) {
                if (sessions.size === 0 && !server.listening) return;
                return closing;
            }
            closed = true;
            const current = [...sessions];
            for (const session of current) session.stop();
            closing = (async () => {
                if (server.listening) await new Promise((resolve) => server.close(resolve));
                const confirmations = await Promise.all(current.map((session) => session.finished));
                if (confirmations.some((confirmed) => !confirmed)) throw new Error('bridge child exit unconfirmed');
            })();
            return closing;
        }
    };
}

async function main() {
    const bridge = createRedisBridge(await loadRedisConfig());
    await bridge.start(); // 端口占用时直接失败，不停止/替换未知监听者。
    console.log('项目Redis stdio桥前台监听127.0.0.1:6380；未启用API缓存。');
    let stopping = false;
    for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, async () => {
        if (stopping) return;
        stopping = true;
        try { await bridge.close(); }
        catch { console.error('项目Redis桥收尾未能确认所有子进程退出，受控连接已关闭。'); process.exitCode = 1; }
    });
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main().catch(() => {
    console.error('项目Redis桥启动失败；未修改未知端口或进程，未启用API缓存。');
    process.exitCode = 1;
});
