import { randomBytes, createHash } from 'node:crypto';
import { access, mkdir, open, readFile, rename, stat, unlink, writeFile } from 'node:fs/promises';
import { createReadStream, createWriteStream } from 'node:fs';
import { spawn } from 'node:child_process';
import { pipeline } from 'node:stream/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import mysql from 'mysql2/promise';
import { loadConfig } from '../src/server.mjs';

/** 排他open成功立即取得所有权；写失败也close/unlink，EEXIST不删除别人文件。 */
export async function preparePrivateOptionFile(filename, contents, { openFile = open, unlinkFile = unlink,
    onCleanupFailure = (target) => console.error(`临时本机凭证选项文件收尾失败，请主管检查此唯一文件：${target}`) } = {}) {
    let handle;
    let owned = false;
    const cleanup = async () => {
        if (handle) {
            try { await handle.close(); }
            catch { onCleanupFailure(filename); }
            handle = undefined;
        }
        if (owned) {
            owned = false;
            try { await unlinkFile(filename); }
            catch { onCleanupFailure(filename); }
        }
    };
    try {
        handle = await openFile(filename, 'wx', 0o600);
        owned = true; // 即使后续写入ENOSPC，创建出的文件也已属于本次操作。
        await handle.writeFile(contents, { encoding: 'utf8' });
        await handle.close();
        handle = undefined;
        return cleanup;
    } catch (error) {
        await cleanup();
        throw error;
    }
}

/** 只备份已核实的本机共享库，stdout二进制落盘；失败保留partial，不导入或清理数据库。 */
async function main() {
    if (process.platform !== 'win32' || !process.env.LOCALAPPDATA) throw new Error('unsupported platform');
    const config = await loadConfig();
    if (config.mysql.host !== '127.0.0.1' || config.mysql.port !== 3310
        || config.mysql.user !== 'ev_app' || config.mysql.database !== 'ev_shared') throw new Error('unsafe target');
    const localRoot = path.join(process.env.LOCALAPPDATA, 'EquipmentViewerSimulation');
    if (!/^[\x20-\x7e]+$/.test(localRoot)) throw new Error('non ASCII private path');
    const marker = JSON.parse(await readFile(path.join(localRoot, 'mysql-credentials.local.json'), 'utf8'));
    const normalize = (value) => path.resolve(value.replace(/\//g, '\\')).toLowerCase().replace(/[\\/]+$/, '');
    const expectedDirectory = path.join(localRoot, 'mysql-data');
    if (marker.purpose !== 'equipment-viewer-simulation-mysql-v1' || marker.stage !== 'configured' || marker.port !== 3310
        || normalize(marker.dataDirectory) !== normalize(expectedDirectory)
        || marker.appPassword !== config.mysql.password || !/^[a-f0-9]{64}$/.test(marker.appPassword))
        throw new Error('marker mismatch');
    const executable = 'C:\\Program Files\\MySQL\\MySQL Server 8.0\\bin\\mysqldump.exe';
    await access(executable);
    const connection = await mysql.createConnection({ host: config.mysql.host, port: config.mysql.port,
        user: config.mysql.user, password: config.mysql.password, database: config.mysql.database,
        connectTimeout: 5000, multipleStatements: false });
    const query = (sql) => connection.execute({ sql, timeout: 5000 });
    let privateFile;
    let privateFileCleanup;
    try {
        const [identity] = await query('SELECT @@datadir AS dataDirectory,@@port AS port,DATABASE() AS databaseName,CURRENT_USER() AS account');
        if (normalize(identity[0].dataDirectory) !== normalize(expectedDirectory) || Number(identity[0].port) !== 3310
            || identity[0].databaseName !== 'ev_shared' || identity[0].account !== 'ev_app@127.0.0.1') throw new Error('instance mismatch');
        const inventorySql = `SELECT TABLE_NAME,ENGINE FROM information_schema.TABLES
            WHERE TABLE_SCHEMA=DATABASE() AND TABLE_TYPE='BASE TABLE' ORDER BY TABLE_NAME`;
        const [tables] = await query(inventorySql);
        if (tables.length === 0 || tables.some((table) => table.ENGINE !== 'InnoDB')) throw new Error('non transactional schema');
        const recoveryRoot = fileURLToPath(new URL('../../.recovery/sql/', import.meta.url));
        await mkdir(recoveryRoot, { recursive: true });
        const stamp = new Date().toISOString().replace(/[:.]/g, '-') + '-' + randomBytes(4).toString('hex');
        const outputDirectory = path.join(recoveryRoot, stamp);
        await mkdir(outputDirectory);
        const partial = path.join(outputDirectory, 'ev_shared.sql.partial');
        const final = path.join(outputDirectory, 'ev_shared.sql');
        privateFile = path.join(localRoot, `backup-${process.pid}-${randomBytes(8).toString('hex')}.local.ini`);
        // 密码只进忽略的ASCII本机选项文件；defaults-file必须首参数，子进程参数不含密码。
        privateFileCleanup = await preparePrivateOptionFile(privateFile,
            `[client]\nhost=127.0.0.1\nport=3310\nuser=ev_app\npassword=${marker.appPassword}\n`);
        const child = spawn(executable, [`--defaults-file=${privateFile}`, '--single-transaction', '--no-tablespaces',
            '--set-gtid-purged=OFF', '--skip-lock-tables', '--skip-add-locks', '--skip-add-drop-table', '--hex-blob',
            '--default-character-set=utf8mb4', '--column-statistics=0', 'ev_shared'],
            { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
        child.stderr.resume(); // 工具错误可能携带敏感资料，不向stdout转发。
        const exited = new Promise((resolve, reject) => {
            child.once('error', reject);
            child.once('close', (code) => code === 0 ? resolve() : reject(new Error('dump exit failure')));
        });
        const timer = setTimeout(() => child.kill(), 120000);
        try { await Promise.all([pipeline(child.stdout, createWriteStream(partial, { flags: 'wx', mode: 0o600 })), exited]); }
        catch (error) { child.kill(); throw error; }
        finally { clearTimeout(timer); }
        const [after] = await query(inventorySql);
        if (JSON.stringify(after) !== JSON.stringify(tables)) throw new Error('schema changed during backup');
        const details = await stat(partial);
        if (details.size === 0) throw new Error('empty dump');
        const hash = createHash('sha256');
        for await (const chunk of createReadStream(partial)) hash.update(chunk);
        const sha256 = hash.digest('hex');
        await writeFile(path.join(outputDirectory, 'manifest.json'), JSON.stringify({ createdAt: new Date().toISOString(),
            database: 'ev_shared', port: 3310, method: 'mysqldump single-transaction', tables: tables.map((table) => table.TABLE_NAME),
            bytes: details.size, sha256, restoreVerified: false }, null, 4), { flag: 'wx' });
        await rename(partial, final);
        console.log(`本机共享库一致性dump已保存：${outputDirectory}；未改数据库，尚未执行恢复验证。`);
    } finally {
        try { connection.destroy(); }
        finally { await privateFileCleanup?.(); }
    }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main().catch(() => {
    console.error('共享库备份失败；数据库未修改，已有partial保留。请核实本机标记、端口、专属配置与mysqldump权限。');
    process.exitCode = 1;
});
