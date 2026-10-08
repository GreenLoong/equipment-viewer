import net from 'node:net';
import { boundedGetScript } from '../src/cache.mjs';

// 仅测试官方客户端真实TCP/RESP与毁socket；Lua行为在替身模拟，不冒充真实Redis执行。
function parseCommand(buffer) {
    let position = 0;
    const readLine = () => {
        const end = buffer.indexOf('\r\n', position);
        if (end < 0) return null;
        const value = buffer.subarray(position, end).toString('ascii');
        position = end + 2;
        return value;
    };
    const header = readLine();
    if (header === null) return null;
    if (!/^\*\d+$/.test(header) || Number(header.slice(1)) > 64) throw new Error('invalid test command');
    const values = [];
    for (let index = 0; index < Number(header.slice(1)); ++index) {
        const line = readLine();
        if (line === null) return null;
        if (!/^\$\d+$/.test(line)) throw new Error('invalid test bulk');
        const length = Number(line.slice(1));
        if (length > 7 * 1024 * 1024) throw new Error('test bulk limit');
        if (buffer.length < position + length + 2) return null;
        values.push(Buffer.from(buffer.subarray(position, position + length)));
        position += length + 2;
    }
    return { values, length: position };
}
function reply(value) {
    if (Array.isArray(value)) return Buffer.concat([Buffer.from(`*${value.length}\r\n`), ...value.map(reply)]);
    if (typeof value === 'number') return Buffer.from(`:${value}\r\n`);
    const bytes = Buffer.isBuffer(value) ? value : Buffer.from(value);
    return Buffer.concat([Buffer.from(`$${bytes.length}\r\n`), bytes, Buffer.from('\r\n')]);
}

export async function createRespCachePeer() {
    const peer = { entries: new Map(), sockets: new Set(), connections: 0, auths: 0, evals: 0, sets: 0,
        authHang: false, evalHang: false, setFail: false, maximumValueSent: 0 };
    const server = net.createServer((socket) => {
        peer.sockets.add(socket); peer.connections += 1;
        let buffer = Buffer.alloc(0);
        let authenticated = false;
        socket.on('error', () => {});
        socket.once('close', () => peer.sockets.delete(socket));
        socket.on('data', (chunk) => {
            buffer = Buffer.concat([buffer, chunk]);
            if (buffer.length > 7 * 1024 * 1024) { socket.destroy(); return; }
            try {
                while (true) {
                    const parsed = parseCommand(buffer);
                    if (!parsed) break;
                    buffer = buffer.subarray(parsed.length);
                    const args = parsed.values;
                    const command = args[0].toString();
                    if (command === 'AUTH') {
                        peer.auths += 1;
                        if (!peer.authHang) { authenticated = true; socket.write('+OK\r\n'); }
                    } else if (!authenticated) socket.write('-NOAUTH controlled authentication required\r\n');
                    else if (command === 'CLIENT' || command === 'SELECT') socket.write('+OK\r\n');
                    else if (command === 'EVAL') {
                        peer.evals += 1;
                        if (peer.evalHang) continue;
                        if (args[1].toString() !== boundedGetScript) throw new Error('unknown controlled script');
                        const key = args[3].toString(); const maximum = Number(args[4]);
                        const entry = peer.entries.get(key);
                        if (!entry || entry.expires <= Date.now()) { peer.entries.delete(key); socket.write(reply([0])); }
                        else if (entry.type !== 'string' || entry.bytes.length > maximum) { peer.entries.delete(key); socket.write(reply([2])); }
                        else { peer.maximumValueSent = Math.max(peer.maximumValueSent, entry.bytes.length); socket.write(reply([1, entry.bytes])); }
                    } else if (command === 'SET') {
                        peer.sets += 1;
                        if (peer.setFail) socket.write('-ERR controlled SET failure\r\n');
                        else {
                            peer.entries.set(args[1].toString(), { type: 'string', bytes: args[2], expires: Date.now() + Number(args[4]) * 1000 });
                            socket.write('+OK\r\n');
                        }
                    } else if (command === 'DEL') { peer.entries.delete(args[1].toString()); socket.write(':1\r\n'); }
                    else throw new Error('unexpected controlled command');
                }
            } catch { socket.destroy(); }
        });
    });
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    return Object.assign(peer, { port: server.address().port, async close() {
        for (const socket of peer.sockets) socket.destroy();
        if (server.listening) await new Promise((resolve) => server.close(resolve));
    } });
}
