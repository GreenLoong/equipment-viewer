import assert from 'node:assert/strict';
import { randomBytes, randomUUID, createHash } from 'node:crypto';
import { access, mkdir, readFile, rename, stat, writeFile } from 'node:fs/promises';
import { createReadStream, createWriteStream } from 'node:fs';
import { spawn } from 'node:child_process';
import { pipeline } from 'node:stream/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import mysql from '../api/node_modules/mysql2/promise.js';
import { createApp, loadConfig } from '../api/src/server.mjs';
import { preparePrivateOptionFile } from '../api/tools/backup-mysql.mjs';

// 受控恢复模拟：仅本轮从空ev_test取得的4表，不导入用户给定SQL，不触ev_shared/原服务。
const root = fileURLToPath(new URL('..', import.meta.url));
const normalize = (value) => path.resolve(value.replace(/\//g, '\\')).toLowerCase().replace(/[\\/]+$/, '');
const dumpTool = 'C:\\Program Files\\MySQL\\MySQL Server 8.0\\bin\\mysqldump.exe';
const restoreTool = 'C:\\Program Files\\MySQL\\MySQL Server 8.0\\bin\\mysql.exe';
const equipmentId = "恢复/设备 '精确' ";
const partId = '零件/支架 ';
let connection;
let app;
let ownedTables = false;
let privateCleanup;
let outputDirectory;
let manifest;
let stage = 'preflight';
let unconfirmedCli = false;

function fixture(id) {
    return { equipmentId: id, equipmentName: "中文设备 '恢复样例'", parts: [
        { id: partId, name: "同名'零件", material: "钢'材", shape: 'box', dimensions: [100, 80, 60], position: [0, -2.5, 0] },
        { id: '..', name: "同名'零件", material: '铝', shape: 'cylinder', dimensions: [10, 80], position: [160, 0, 0] }
    ] };
}

async function sha256(filename) {
    const hash = createHash('sha256');
    for await (const chunk of createReadStream(filename)) hash.update(chunk);
    return hash.digest('hex');
}

function utc(dto) {
    assert.ok(/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/.test(dto.createdAt), 'createdAt必须UTC ISO');
    assert.ok(/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/.test(dto.updatedAt), 'updatedAt必须UTC ISO');
    assert.ok(Number.isFinite(Date.parse(dto.createdAt)) && Number.isFinite(Date.parse(dto.updatedAt)));
}

async function saveManifest() {
    // 该目录为本轮排他创建，manifest不含凭证或SQL正文。
    await writeFile(path.join(outputDirectory, 'manifest.json'), JSON.stringify(manifest, null, 4) + '\n', { mode: 0o600 });
}

/** CLI管道失败/超时后必须确认close；未确认时由调用方保留本轮测试表。 */
export async function runRestoreCli(executable, args, transfer, {
    spawnProcess = spawn, timeoutMs = 120000, reapTimeoutMs = 2000, onUnconfirmed = () => {}
} = {}) {
    const child = spawnProcess(executable, args, { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
    child.stderr.resume(); // 工具错误可能含正文/凭证，不输出其内容或参数。
    let confirmed = false;
    let spawnFailed = false;
    let resolveClosed;
    const closed = new Promise((resolve) => { resolveClosed = resolve; });
    const exited = new Promise((resolve, reject) => {
        child.on('error', () => {
            spawnFailed = !child.pid; // 没有PID的spawn失败说明没有CLI可以继续写入。
            reject(new Error('controlled CLI spawn failed'));
        });
        child.once('close', (code) => {
            confirmed = true;
            resolveClosed();
            if (code === 0) resolve();
            else reject(new Error('controlled CLI exit failed'));
        });
    });
    let timer;
    const deadline = new Promise((_, reject) => {
        timer = setTimeout(() => {
            const error = new Error('controlled CLI deadline');
            error.code = 'CLI_TIMEOUT';
            reject(error);
        }, timeoutMs);
    });
    try {
        // 除了等待结果，还在失败路径实际破坏管道/终止子进程并确认close。
        await Promise.race([Promise.all([Promise.resolve().then(() => transfer(child)), exited]), deadline]);
    } catch (cause) {
        clearTimeout(timer);
        for (const stream of [child.stdin, child.stdout, child.stderr]) stream?.destroy();
        if (!confirmed && !spawnFailed) {
            try { child.kill(); }
            catch { /* kill失败也必须等close，不能绕过未确认退出守卫。 */ }
        }
        let reapTimer;
        const reaped = confirmed || spawnFailed || await Promise.race([
            closed.then(() => true),
            new Promise((resolve) => { reapTimer = setTimeout(() => resolve(false), reapTimeoutMs); })
        ]);
        clearTimeout(reapTimer);
        const error = new Error(reaped ? 'controlled CLI failed after confirmed exit' : 'controlled CLI exit unconfirmed');
        error.exitConfirmed = Boolean(reaped);
        error.code = reaped ? (cause.code ?? 'CLI_FAILED') : 'CLI_EXIT_UNCONFIRMED';
        if (!reaped) {
            onUnconfirmed({ pid: child.pid ?? null, executable }); // 不提供参数/凭证/SQL。
            try { child.unref?.(); } catch { /* 守卫已通知调用方，仍以未确认退出拒绝。 */ }
        }
        throw error;
    } finally { clearTimeout(timer); }
}

async function main() {
const cliOptions = { onUnconfirmed: ({ pid, executable }) => {
    unconfirmedCli = true;
    console.error(`RESTORE_SIMULATION cliExitConfirmed=0 ownedPid=${pid} executable=${executable}；保留本轮ev_test，不清表。`);
} };
try {
    assert.equal(process.platform, 'win32', '仅本机Windows受控实例');
    assert.ok(process.env.EV_TEST_CONFIG_FILE, '必须明确EV_TEST_CONFIG_FILE，不能默认正常配置');
    assert.ok(process.env.LOCALAPPDATA, '需要本机私有选项目录');
    const config = await loadConfig(process.env.EV_TEST_CONFIG_FILE);
    assert.equal(config.mysql.host, '127.0.0.1');
    assert.equal(config.mysql.port, 3310, '禁止原MySQL80');
    assert.equal(config.mysql.user, 'ev_app');
    assert.equal(config.mysql.database, 'ev_test', '禁止正常共享库恢复');
    const localRoot = path.join(process.env.LOCALAPPDATA, 'EquipmentViewerSimulation');
    assert.ok(/^[\x20-\x7e]+$/.test(localRoot), '临时凭证路径必须ASCII');
    const expectedDirectory = path.join(localRoot, 'mysql-data');
    const marker = JSON.parse(await readFile(path.join(localRoot, 'mysql-credentials.local.json'), 'utf8'));
    assert.equal(marker.purpose, 'equipment-viewer-simulation-mysql-v1');
    assert.equal(marker.stage, 'configured');
    assert.equal(marker.port, 3310);
    assert.equal(normalize(marker.dataDirectory), normalize(expectedDirectory));
    // 限定已核实的64位hex密码，选项文件不会被换行/引号注入；从不打印比较值。
    assert.ok(/^[a-f0-9]{64}$/.test(marker.appPassword));
    if (marker.appPassword !== config.mysql.password) throw new Error('private credentials mismatch');
    await Promise.all([access(dumpTool), access(restoreTool)]);
    connection = await mysql.createConnection({ ...config.mysql, charset: 'utf8mb4', connectTimeout: 5000, multipleStatements: false });
    const query = (sql, values = []) => connection.execute({ sql, timeout: 5000 }, values);
    const inventory = () => query('SELECT TABLE_NAME,ENGINE FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() ORDER BY TABLE_NAME');
    const [identity] = await query('SELECT @@datadir AS dataDirectory,@@port AS port,DATABASE() AS databaseName,CURRENT_USER() AS account');
    assert.equal(normalize(identity[0].dataDirectory), normalize(expectedDirectory));
    assert.equal(Number(identity[0].port), 3310);
    assert.equal(identity[0].databaseName, 'ev_test');
    assert.equal(identity[0].account, 'ev_app@127.0.0.1');
    const [lock] = await query("SELECT GET_LOCK('equipment_viewer_ev_test_restore',0) AS acquired");
    assert.equal(Number(lock[0].acquired), 1, '已有本恢复测试时拒绝争用');
    const [initial] = await inventory();
    assert.equal(initial.length, 0, 'ev_test非空时不得执行或清理');
    ownedTables = true;

    // 仅选取当前业务API必需配置；恢复验证不依赖缓存或正常API监听地址。
    const serviceConfig = { host: '127.0.0.1', port: 0, token: config.token, mysql: config.mysql,
        operationTimeoutMs: config.operationTimeoutMs, closeTimeoutMs: config.closeTimeoutMs };
    app = await createApp(serviceConfig);
    let address = await app.start(0, '127.0.0.1');
    let base = `http://127.0.0.1:${address.port}`;
    const request = async (route, method = 'GET', body) => {
        const response = await fetch(base + route, { method, headers: { Authorization: `Bearer ${config.token}`,
            ...(body ? { 'Content-Type': 'application/json' } : {}) },
            ...(body ? { body: JSON.stringify(body) } : {}), signal: AbortSignal.timeout(15000) });
        return { status: response.status, json: await response.json() };
    };
    stage = 'creating-controlled-data';
    assert.equal((await request('/api/equipment', 'POST', fixture(equipmentId))).status, 201);
    assert.equal((await request('/api/equipment', 'POST', fixture('..'))).status, 201);
    const creation = { equipmentId, partId, text: "初始工艺意见 'A'\n保留换行。", author: '工艺甲', requestId: randomUUID() };
    const created = await request('/api/annotations', 'POST', creation);
    assert.equal(created.status, 201);
    const changed = await request('/api/annotations/' + creation.requestId, 'PUT', {
        text: "已更新版本2\n确认 'O' 形圈。", editor: '工艺乙', expectedRevision: 1 });
    assert.equal(changed.status, 200);
    const expectedNote = changed.json.data;
    assert.equal(expectedNote.revision, 2);
    assert.equal(expectedNote.author, '工艺甲');
    assert.equal(expectedNote.updatedBy, '工艺乙');
    assert.equal(expectedNote.createdAt, created.json.data.createdAt);
    utc(expectedNote);
    const [beforeEpoch] = await query('SELECT UNIX_TIMESTAMP(created_at)*1000 AS createdEpoch,UNIX_TIMESTAMP(updated_at)*1000 AS updatedEpoch FROM annotations WHERE id=?', [creation.requestId]);
    await app.close(); // dump前停止本轮写入者，不停止MySQL实例。

    stage = 'dumping-owned-schema';
    const [tables] = await inventory();
    assert.deepEqual(tables.map((item) => item.TABLE_NAME), ['annotations', 'equipment', 'part', 'schema_version']);
    assert.ok(tables.every((item) => item.ENGINE === 'InnoDB'));
    const recoveryRoot = path.join(root, '.recovery', 'tests');
    await mkdir(recoveryRoot, { recursive: true });
    outputDirectory = path.join(recoveryRoot, new Date().toISOString().replace(/[:.]/g, '-') + '-' + randomBytes(4).toString('hex'));
    assert.equal(path.dirname(path.resolve(outputDirectory)), path.resolve(recoveryRoot));
    await mkdir(outputDirectory); // 本轮排他新目录；失败保留，不删dump证据。
    const partial = path.join(outputDirectory, 'ev_test.sql.partial');
    const dumpFile = path.join(outputDirectory, 'ev_test.sql');
    const optionFile = path.join(localRoot, `restore-test-${process.pid}-${randomBytes(8).toString('hex')}.local.ini`);
    privateCleanup = await preparePrivateOptionFile(optionFile,
        `[client]\nhost=127.0.0.1\nport=3310\nuser=ev_app\npassword=${marker.appPassword}\n`, {
            onCleanupFailure: (filename) => {
                console.error(`RESTORE_SIMULATION privateFileCleanup=0；只检查本次临时选项文件：${filename}`);
                process.exitCode = 1;
            }
        });
    await runRestoreCli(dumpTool, [`--defaults-file=${optionFile}`, '--single-transaction', '--no-tablespaces',
        '--set-gtid-purged=OFF', '--skip-lock-tables', '--skip-add-locks', '--skip-add-drop-table', '--skip-triggers',
        '--hex-blob', '--tz-utc', '--default-character-set=utf8mb4', '--column-statistics=0',
        'ev_test', 'equipment', 'part', 'annotations', 'schema_version'], async (child) => {
        child.stdin.end();
        await pipeline(child.stdout, createWriteStream(partial, { flags: 'wx', mode: 0o600 }));
    }, cliOptions);
    const details = await stat(partial);
    assert.ok(details.size > 0 && details.size < 5 * 1024 * 1024, '受控小样例dump需非空且有界');
    const digest = await sha256(partial);
    manifest = { purpose: 'equipment-viewer-ev-test-restore', database: 'ev_test', port: 3310,
        createdAt: new Date().toISOString(), tables: tables.map((item) => item.TABLE_NAME),
        file: 'ev_test.sql', bytes: details.size, sha256: digest, method: 'single-transaction/tz-utc',
        restored: false, restoreVerified: false, normalDatabaseRestore: false };
    await saveManifest();
    await rename(partial, dumpFile);

    stage = 'verifying-dump-before-clear';
    const savedManifest = JSON.parse(await readFile(path.join(outputDirectory, 'manifest.json'), 'utf8'));
    assert.equal(savedManifest.purpose, 'equipment-viewer-ev-test-restore');
    assert.equal(savedManifest.database, 'ev_test');
    assert.equal(savedManifest.port, 3310);
    assert.equal(savedManifest.file, 'ev_test.sql');
    assert.equal((await stat(dumpFile)).size, savedManifest.bytes);
    assert.equal(await sha256(dumpFile), savedManifest.sha256, '恢复前必须核对本轮dump哈希');
    const controlledSql = await readFile(dumpFile, 'utf8');
    assert.ok(!/\b(?:CREATE|DROP)\s+DATABASE\b|\bUSE\s+/i.test(controlledSql), 'dump不得切换或创建数据库');
    const [unchanged] = await inventory();
    assert.deepEqual(unchanged, tables, 'dump期间schema不得变化');

    stage = 'clearing-only-owned-tables';
    for (const table of ['annotations', 'part', 'equipment', 'schema_version']) await query(`DROP TABLE IF EXISTS ${table}`);
    assert.equal((await inventory())[0].length, 0);
    // 不接受外部dump路径；只有本轮生成、哈希核对过的SQL可以进入指定ev_test。
    stage = 'restoring-controlled-dump';
    assert.equal(await sha256(dumpFile), savedManifest.sha256);
    await runRestoreCli(restoreTool, [`--defaults-file=${optionFile}`, '--protocol=TCP', '--database=ev_test',
        '--default-character-set=utf8mb4', '--binary-mode=1'], async (child) => {
        child.stdout.resume();
        await pipeline(createReadStream(dumpFile), child.stdin);
    }, cliOptions);
    manifest.restored = true;
    await saveManifest();

    stage = 'reopening-api-and-verifying';
    app = await createApp(serviceConfig);
    address = await app.start(0, '127.0.0.1');
    base = `http://127.0.0.1:${address.port}`;
    for (const id of [equipmentId, '..']) {
        const actual = await request('/api/equipment?id=' + encodeURIComponent(id));
        assert.equal(actual.status, 200);
        assert.deepEqual(actual.json.data, fixture(id), '恢复后geometry与精确身份需一致');
    }
    const notesRoute = '/api/annotations?equipmentId=' + encodeURIComponent(equipmentId) + '&partId=' + encodeURIComponent(partId);
    assert.deepEqual((await request(notesRoute)).json.data, [expectedNote], '恢复后批注DTO/UTC/修订必须完全一致');
    const [afterEpoch] = await query('SELECT UNIX_TIMESTAMP(created_at)*1000 AS createdEpoch,UNIX_TIMESTAMP(updated_at)*1000 AS updatedEpoch FROM annotations WHERE id=?', [creation.requestId]);
    assert.deepEqual(afterEpoch, beforeEpoch, '恢复前后真实存储epoch保持');
    const retry = await request('/api/annotations', 'POST', creation);
    assert.equal(retry.status, 200);
    assert.deepEqual(retry.json.data, expectedNote, '原payload指纹随dump恢复，相同UUID不重建旧内容');
    const stale = await request('/api/annotations/' + creation.requestId, 'PUT', { text: '旧版本不能覆盖', editor: '工艺丙', expectedRevision: 1 });
    assert.equal(stale.status, 409);
    assert.equal(stale.json.error.code, 'REVISION_CONFLICT');
    assert.deepEqual((await request(notesRoute)).json.data, [expectedNote], '恢复后的旧CAS拒绝且保持版本2');
    const latest = await request('/api/annotations/' + creation.requestId, 'PUT', { text: '恢复验证后显式版本3', editor: '工艺丙', expectedRevision: 2 });
    assert.equal(latest.status, 200);
    assert.equal(latest.json.data.revision, 3);
    assert.equal(latest.json.data.author, '工艺甲');
    assert.equal(latest.json.data.updatedBy, '工艺丙');
    utc(latest.json.data);
    const [counts] = await query('SELECT (SELECT COUNT(*) FROM equipment) AS equipmentCount,(SELECT COUNT(*) FROM part) AS partCount,(SELECT COUNT(*) FROM annotations) AS annotationCount');
    assert.equal(Number(counts[0].equipmentCount), 2);
    assert.equal(Number(counts[0].partCount), 4);
    assert.equal(Number(counts[0].annotationCount), 1);
    manifest.restoreVerified = true;
    manifest.verifiedAt = new Date().toISOString();
    manifest.checks = ['exact-geometry', 'exact-equipment-part-ids', 'annotation-revision2', 'utc-epoch', 'original-request-idempotency', 'stale-cas-rejected', 'explicit-revision3'];
    await saveManifest();
    console.log(`RESTORE_SIMULATION accepted=1 database=ev_test port=3310 dto=1 utc=1 cas=1 geometry=1 evidence=${outputDirectory}`);
} catch {
    console.error(`RESTORE_SIMULATION accepted=0 stage=${stage}；仅受控ev_test测试，本轮dump/partial保留，未恢复正常库。`);
    process.exitCode = 1;
} finally {
    try {
        await app?.close();
        if (unconfirmedCli) {
            console.error('RESTORE_SIMULATION cleanup=0 ownedTestTablesKept=1；CLI退出未确认，不发DROP。');
            process.exitCode = 1;
        } else if (ownedTables && connection) {
            for (const table of ['annotations', 'part', 'equipment', 'schema_version'])
                await connection.execute({ sql: `DROP TABLE IF EXISTS ${table}`, timeout: 5000 });
            const [remaining] = await connection.execute({ sql: 'SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE()', timeout: 5000 });
            assert.equal(remaining.length, 0, '只清本轮表后ev_test应空');
            console.log('RESTORE_SIMULATION cleanup=1 ownedTestTablesRemoved=1');
        }
    } catch {
        console.error('RESTORE_SIMULATION cleanup=0；检查本轮ev_test状态，保留正常库和dump证据。');
        process.exitCode = 1;
    } finally {
        try { connection?.destroy(); }
        finally { await privateCleanup?.(); }
    }
}
}

// 单元测试import只取得CLI helper，绝不连接MySQL/运行恢复主流程。
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main().catch(() => {
    console.error('RESTORE_SIMULATION accepted=0；受控恢复入口失败，保留证据与正常资料。');
    process.exitCode = 1;
});
