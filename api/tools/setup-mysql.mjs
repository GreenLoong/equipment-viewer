import { randomBytes } from 'node:crypto';
import { access, mkdir, readFile, readdir, rename, writeFile } from 'node:fs/promises';
import { spawn, execFile } from 'node:child_process';
import net from 'node:net';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';
import mysql from 'mysql2/promise';

const executeFile = promisify(execFile);
const binary = 'C:\\Program Files\\MySQL\\MySQL Server 8.0\\bin\\mysqld.exe';
const port = 3310;
const runtime = fileURLToPath(new URL('../runtime/', import.meta.url));
const purpose = 'equipment-viewer-simulation-mysql-v1';
let diagnosticStage = 'preflight';
const wait = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
const normalized = (value) => path.resolve(value.replace(/\//g, '\\')).toLowerCase().replace(/[\\/]+$/, '');
const secret = () => randomBytes(32).toString('hex');

/** 核对固定身份和连接核心，允许本机配置增加超时/缓存等独立字段，不改写文件。 */
export function matchesExpectedCore(current, expected) {
    if (expected !== null && typeof expected === 'object') {
        return current !== null && typeof current === 'object' && !Array.isArray(current)
            && Object.entries(expected).every(([key, value]) => matchesExpectedCore(current[key], value));
    }
    return current === expected;
}

/** 客户端DTO仅能含HTTP主机/端口/令牌，禁止把MySQL凭证交给GUI。 */
export function isClientConfig(current, expected) {
    return matchesExpectedCore(current, expected)
        && Object.keys(current).sort().join(',') === 'host,port,token';
}

async function exists(filename) { try { await access(filename); return true; } catch { return false; } }

/** 秘密文件用临时文件+rename保存，避免中断留下半份凭证；不进入仓库。 */
async function saveJson(filename, value) {
    const temporary = filename + '.pending';
    await writeFile(temporary, JSON.stringify(value, null, 4) + '\n', { mode: 0o600 });
    await rename(temporary, filename);
}

async function portBusy() {
    return new Promise((resolve) => {
        const socket = net.connect({ host: '127.0.0.1', port });
        const finish = (busy) => { socket.destroy(); resolve(busy); };
        socket.setTimeout(1000, () => finish(true));
        socket.once('connect', () => finish(true));
        socket.once('error', (error) => finish(error.code !== 'ECONNREFUSED'));
    });
}

/** 核实PID对应安装的mysqld和本工具配置；不向未知PID发信号。 */
async function verifyPid(pidFile, configFile) {
    const pidText = (await readFile(pidFile, 'utf8')).trim();
    if (!/^\d+$/.test(pidText)) throw new Error('项目PID文件无效。');
    const pid = Number(pidText);
    const script = `$p = Get-CimInstance Win32_Process -Filter 'ProcessId=${pid}'; if ($p) { @{ path=$p.ExecutablePath; command=$p.CommandLine } | ConvertTo-Json -Compress }`;
    const { stdout } = await executeFile('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', script],
        { windowsHide: true, timeout: 10000, maxBuffer: 32768 });
    const processInfo = JSON.parse(stdout);
    if (typeof processInfo.path !== 'string' || normalized(processInfo.path) !== normalized(binary)
        || typeof processInfo.command !== 'string'
        || !processInfo.command.toLowerCase().includes(`--defaults-file=${configFile}`.toLowerCase()))
        throw new Error('PID未能核实为本项目MySQL，拒绝操作。');
    return pid;
}

/** 认证与@@datadir双重核实实例身份；未配置阶段允许恢复首次设密过程。 */
async function connectOwned(marker, dataDirectory) {
    let connection;
    const passwords = marker.stage === 'configured' ? [marker.rootPassword] : [marker.rootPassword, ''];
    for (const password of passwords) {
        try {
            connection = await mysql.createConnection({ host: '127.0.0.1', port, user: 'root', password,
                connectTimeout: 2000, multipleStatements: false });
            break;
        } catch { /* 只在首次设密阶段尝试恢复，不改任何账号。 */ }
    }
    if (!connection) throw new Error('3310认证失败，拒绝把未知实例当作项目实例。');
    try {
        const [rows] = await connection.execute('SELECT @@datadir AS dataDirectory, @@port AS port');
        if (normalized(rows[0].dataDirectory) !== normalized(dataDirectory) || Number(rows[0].port) !== port)
            throw new Error('3310数据目录不属于本项目，拒绝操作。');
        return connection;
    } catch (error) { await connection.end(); throw error; }
}

/** 显式本机独立实例初始化/恢复/停止；不触原服务，不启动HTTP API。 */
async function main() {
    if (process.platform !== 'win32' || !process.env.LOCALAPPDATA)
        throw new Error('此工具只用于Windows本机独立实例。');
    const operation = process.argv[2] ?? 'setup';
    if (!['setup', 'stop'].includes(operation)) throw new Error('只允许setup或stop。');
    const root = path.join(process.env.LOCALAPPDATA, 'EquipmentViewerSimulation');
    const dataDirectory = path.join(root, 'mysql-data');
    const configFile = path.join(root, 'mysql-project.ini');
    const pidFile = path.join(root, 'mysql-project.pid');
    const markerFile = path.join(root, 'mysql-credentials.local.json');
    if (!/^[\x20-\x7e]+$/.test(root)) throw new Error('MySQL实例路径必须为ASCII；保留现有目录，请主管调整专属运行路径。');
    await access(binary);
    let marker;
    if (await exists(markerFile)) {
        try { marker = JSON.parse(await readFile(markerFile, 'utf8')); } catch { throw new Error('凭证标记损坏；保留目录并停止。'); }
        if (marker.purpose !== purpose || marker.port !== port || normalized(marker.dataDirectory) !== normalized(dataDirectory)
            || !['initialized', 'configured'].includes(marker.stage)
            || ![marker.rootPassword, marker.appPassword, marker.token].every((value) => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value)))
            throw new Error('凭证标记不兼容；保留目录并停止。');
    }
    const busy = await portBusy();
    if (busy && !marker) throw new Error('3310已占用但无项目标记；拒绝初始化或操作该实例。');
    if (operation === 'stop') {
        if (!marker) throw new Error('无项目标记，拒绝停止任何进程。');
        if (!busy) { console.log('项目MySQL端口未监听；未操作其他进程。'); return; }
        const connection = await connectOwned(marker, dataDirectory);
        try {
            await verifyPid(pidFile, configFile);
            // SQL SHUTDOWN由已核实的独立实例执行，避免Windows强制kill破坏数据库。
            await connection.query('SHUTDOWN');
        } finally { await connection.end().catch(() => {}); }
        for (let attempt = 0; attempt < 100 && await portBusy(); ++attempt) await wait(100);
        if (await portBusy()) throw new Error('项目MySQL仍在停止中；未强制终止进程。');
        marker.pid = null;
        await saveJson(markerFile, marker);
        console.log('项目独立MySQL已安全停止；原MySQL80未操作。');
        return;
    }
    await mkdir(root, { recursive: true });
    const contents = await exists(dataDirectory) ? await readdir(dataDirectory) : [];
    if (!marker && contents.length > 0) throw new Error('已有非空MySQL目录但无初始化标记，保留数据并停止。');
    if (marker && contents.length === 0) throw new Error('标记存在但数据目录为空，拒绝重建。');
    if (!marker) {
        await mkdir(dataDirectory, { recursive: true });
        // 仅在已确认空目录执行；该命令不读取全局配置，也不注册Windows服务。
        diagnosticStage = 'initialize';
        await executeFile(binary, ['--no-defaults', '--initialize-insecure', `--datadir=${dataDirectory}`],
            { windowsHide: true, timeout: 120000, maxBuffer: 1024 * 1024 });
        marker = { purpose, port, dataDirectory, stage: 'initialized', rootPassword: secret(), appPassword: secret(), token: secret(), pid: null };
        await saveJson(markerFile, marker);
    }
    const forward = (value) => value.replace(/\\/g, '/');
    const expectedConfig = `[mysqld]\nbasedir="${forward(path.dirname(path.dirname(binary)))}"\ndatadir="${forward(dataDirectory)}"\nport=3310\nbind-address=127.0.0.1\nmysqlx=0\npid-file="${forward(pidFile)}"\nlog-error="${forward(path.join(root, 'mysql-project.log'))}"\ncharacter-set-server=utf8mb4\ncollation-server=utf8mb4_0900_bin\n`;
    if (await exists(configFile)) {
        if (await readFile(configFile, 'utf8') !== expectedConfig)
            throw new Error('项目实例配置已变化，保留文件并停止；需主管核实。');
    } else await writeFile(configFile, expectedConfig, { flag: 'wx' });
    if (!busy) {
        diagnosticStage = 'start';
        // defaults-file必须首参数；独立进程隐藏启动，不注册或修改MySQL80。
        const child = spawn(binary, [`--defaults-file=${configFile}`], { detached: true, windowsHide: true, stdio: 'ignore' });
        await new Promise((resolve, reject) => { child.once('spawn', resolve); child.once('error', reject); });
        child.unref();
    }
    let connection;
    diagnosticStage = 'connect-owned';
    const deadline = Date.now() + 20000;
    while (Date.now() < deadline) {
        try { connection = await connectOwned(marker, dataDirectory); break; }
        catch { await wait(150); }
    }
    if (!connection) throw new Error('独立MySQL未就绪，目录与标记已保留；请主管检查本机日志。');
    try {
        diagnosticStage = 'verify-pid';
        marker.pid = await verifyPid(pidFile, configFile);
        // 本项目本机实例默认开启binlog，触发器故障测试需要此实例级开关；不把SUPER权限授予ev_app。
        diagnosticStage = 'project-trigger-policy';
        await connection.execute('SET GLOBAL log_bin_trust_function_creators = 1');
        if (marker.stage === 'initialized') {
            diagnosticStage = 'root-password';
            // 实测MySQL8账户DDL不接受COM_STMT_PREPARE的密码占位符；query仍由驱动转义绑定，秘密不进参数或日志。
            await connection.query("ALTER USER 'root'@'localhost' IDENTIFIED BY ?", [marker.rootPassword]);
            diagnosticStage = 'create-databases';
            await connection.execute('CREATE DATABASE IF NOT EXISTS ev_shared CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_bin');
            await connection.execute('CREATE DATABASE IF NOT EXISTS ev_test CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_bin');
            diagnosticStage = 'app-user';
            await connection.query("CREATE USER IF NOT EXISTS 'ev_app'@'127.0.0.1' IDENTIFIED BY ?", [marker.appPassword]);
            await connection.query("ALTER USER 'ev_app'@'127.0.0.1' IDENTIFIED BY ?", [marker.appPassword]);
            await connection.execute("GRANT ALL PRIVILEGES ON ev_shared.* TO 'ev_app'@'127.0.0.1'");
            await connection.execute("GRANT ALL PRIVILEGES ON ev_test.* TO 'ev_app'@'127.0.0.1'");
            marker.stage = 'configured';
        }
        await saveJson(markerFile, marker);
    } finally { await connection.end(); }
    diagnosticStage = 'save-config';
    await mkdir(runtime, { recursive: true });
    for (const [filename, database] of [['service.local.json', 'ev_shared'], ['test.local.json', 'ev_test']]) {
        const expected = { host: '127.0.0.1', port: 8765, token: marker.token, mysql: {
            host: '127.0.0.1', port, user: 'ev_app', password: marker.appPassword, database, connectionLimit: 4 } };
        const target = path.join(runtime, filename);
        if (await exists(target)) {
            let current;
            try { current = JSON.parse(await readFile(target, 'utf8')); } catch { throw new Error('现有本机API配置无效，保留文件并停止。'); }
            if (!matchesExpectedCore(current, expected))
                throw new Error('现有本机API配置与实例标记不同，保留文件并停止。');
        } else await saveJson(target, expected);
    }
    const client = { host: '127.0.0.1', port: 8765, token: marker.token };
    const clientFile = path.join(runtime, 'client.local.json');
    if (await exists(clientFile)) {
        let current;
        try { current = JSON.parse(await readFile(clientFile, 'utf8')); }
        catch { throw new Error('现有客户端配置无效，保留文件并停止。'); }
        if (!isClientConfig(current, client)) throw new Error('客户端配置不同或含多余字段，保留文件并停止。');
    } else await saveJson(clientFile, client);
    diagnosticStage = 'app-authentication';
    const appConnection = await mysql.createConnection({ host: '127.0.0.1', port, user: 'ev_app', password: marker.appPassword,
        database: 'ev_shared', connectTimeout: 3000 });
    try { await appConnection.execute('SELECT 1'); } finally { await appConnection.end(); }
    console.log('项目独立MySQL3310已就绪，ev_shared/ev_test与本机配置完成；未启动HTTP API，未操作MySQL80。');
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main().catch((error) => {
    // 底层错误可能带SQL、配置或秘密，统一提示；详细服务诊断在专属本机日志。
    console.error('项目MySQL操作失败；原实例未操作，现有目录保留。请主管检查专属标记、端口、配置和mysql-project.log。');
    const code = typeof error.code === 'string' && /^[A-Z0-9_]+$/.test(error.code) ? error.code : 'LOCAL_VALIDATION';
    console.error(`阶段=${diagnosticStage} 错误码=${code}`);
    process.exitCode = 1;
});
