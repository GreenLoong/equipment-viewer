import test from 'node:test';
import assert from 'node:assert/strict';
import mysql from 'mysql2/promise';
import { createApp, loadConfig } from '../src/server.mjs';
import { maximumBodyBytes } from '../src/validation.mjs';
import { EquipmentRepository } from '../src/repository.mjs';

function sample(id = '设备A') {
    return { equipmentId: id, equipmentName: "中文设备'同名", parts: [
        { id: 'a', name: '同名', material: "钢'材", shape: 'box', dimensions: [1, 2, 3], position: [0, -2, 3] },
        { id: 'A', name: '同名', material: '铝', shape: 'cylinder', dimensions: [2, 10], position: [1, 2, 3] },
        { id: 'a ', name: '同名', material: '钛', shape: 'box', dimensions: [3, 4, 5], position: [6, 7, 8] }
    ] };
}

test('真实MySQL与HTTP：精确身份/重启/并发重复/第三零件SQL失败回滚/边界', { timeout: 180000 }, async (t) => {
    // 测试配置必须明确给定并限定独立3310的专属ev_test；不默认落到生产配置。
    assert.ok(process.env.EV_TEST_CONFIG_FILE, '必须设置EV_TEST_CONFIG_FILE');
    const config = await loadConfig(process.env.EV_TEST_CONFIG_FILE);
    assert.equal(config.mysql.database, 'ev_test', '禁止测试正常共享数据库');
    assert.equal(config.mysql.port, 3310, '禁止测试原MySQL80');
    assert.ok(['127.0.0.1', 'localhost', '::1'].includes(config.mysql.host), '仅项目本机实例');
    assert.equal(config.mysql.user, 'ev_app', '仅项目独立账号');
    const admin = mysql.createPool({ ...config.mysql, charset: 'utf8mb4', multipleStatements: false });
    let app;
    let ownTables = false;
    t.after(async () => {
        try {
            await app?.close();
            if (ownTables) {
                // 仅删除本次在空ev_test中创建的表，外键依赖从子表向父表收尾。
                await admin.execute('DROP TABLE IF EXISTS part');
                await admin.execute('DROP TABLE IF EXISTS equipment');
                await admin.execute('DROP TABLE IF EXISTS schema_version');
            }
        } finally { await admin.end(); }
    });
    const [existing] = await admin.execute('SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE()');
    assert.equal(existing.length, 0, 'ev_test非空时拒绝覆盖或清理旧表，请检查上次测试交接');
    ownTables = true;
    app = await createApp(config);
    let address = await app.start(0);
    let base = `http://127.0.0.1:${address.port}`;
    const request = async (route, options = {}) => {
        const response = await fetch(base + route, { ...options, headers: { Authorization: `Bearer ${config.token}`, ...options.headers },
            signal: AbortSignal.timeout(120000) });
        return { status: response.status, json: await response.json() };
    };
    const post = (value) => request('/api/equipment', { method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: typeof value === 'string' ? value : JSON.stringify(value) });
    assert.equal((await fetch(base + '/health')).status, 200);
    assert.equal((await fetch(base + '/api/equipment')).status, 401);
    assert.equal((await request('/api/equipment/no-such-id')).status, 404);
    assert.equal((await request('/api/equipment', { method: 'DELETE' })).status, 405);
    assert.equal((await request('/api/equipment', { method: 'POST', body: '{}' })).status, 415);
    assert.equal((await post({ equipmentId: 'invalid' })).status, 400);
    assert.equal((await post(' '.repeat(maximumBodyBytes + 1))).status, 413);
    for (const id of ['a', 'A', 'a ', "中文/'?😀", '😀'.repeat(64), '.', '..']) {
        assert.equal((await post(sample(id))).status, 201);
        assert.deepEqual((await request('/api/equipment?id=' + encodeURIComponent(id))).json.data, sample(id));
        if (id !== '.' && id !== '..')
            assert.deepEqual((await request('/api/equipment/' + encodeURIComponent(id))).json.data, sample(id));
    }
    assert.equal((await post(sample('a'))).status, 409);
    const duplicate = await Promise.all([post(sample('concurrent')), post(sample('concurrent'))]);
    assert.deepEqual(duplicate.map((result) => result.status).sort(), [201, 409]);
    const [pad] = await admin.execute("SELECT PAD_ATTRIBUTE FROM information_schema.COLLATIONS WHERE COLLATION_NAME='utf8mb4_0900_bin'");
    assert.equal(pad[0].PAD_ATTRIBUTE, 'NO PAD');
    const [counts] = await admin.execute('SELECT COUNT(*) AS n FROM part WHERE equipment_id=?', ['a']);
    assert.equal(Number(counts[0].n), 3);

    // 真实触发器在第三零件插入时发出SQL错误，验证事务保护而非校验提前拒绝。
    // MySQL8不在预编译协议中支持CREATE TRIGGER；该DDL固定且不含用户值或秘密。
    await admin.query(`CREATE TRIGGER ev_test_fail_third BEFORE INSERT ON part FOR EACH ROW
        BEGIN IF NEW.ordinal=2 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='intentional rollback'; END IF; END`);
    const rollback = await post(sample('rollback'));
    assert.equal(rollback.status, 503);
    assert.equal(JSON.stringify(rollback.json).includes('intentional'), false);
    const [equipmentRows] = await admin.execute('SELECT COUNT(*) AS n FROM equipment WHERE id=?', ['rollback']);
    const [partRows] = await admin.execute('SELECT COUNT(*) AS n FROM part WHERE equipment_id=?', ['rollback']);
    assert.equal(Number(equipmentRows[0].n), 0);
    assert.equal(Number(partRows[0].n), 0);
    await admin.query('DROP TRIGGER ev_test_fail_third');
    assert.equal((await post(sample('rollback'))).status, 201, 'SQL失败后合法发布仍可继续');

    await app.close();
    app = await createApp(config);
    address = await app.start(0);
    base = `http://127.0.0.1:${address.port}`;
    assert.deepEqual((await request('/api/equipment/a')).json.data, sample('a'), '重建服务后持久化读取');
    const list = (await request('/api/equipment')).json.data;
    assert.equal(list.length, 9);
    assert.ok(list.every((item) => item.partCount === 3 && item.revision === 1));
    assert.deepEqual(list, (await request('/api/equipment')).json.data, '列表排序稳定');

    const boundary = { equipmentId: 'parts-10000', equipmentName: '零件边界', parts:
        Array.from({ length: 10000 }, (_, index) => ({ ...sample().parts[0], id: `P${index}` })) };
    assert.equal((await post(boundary)).status, 201);
    assert.deepEqual((await request('/api/equipment/parts-10000')).json.data, boundary);
    boundary.equipmentId = 'parts-10001';
    boundary.parts.push({ ...boundary.parts[0], id: 'P10000' });
    assert.equal((await post(boundary)).status, 413);
    assert.equal((await request('/api/equipment/parts-10001')).status, 404);
    const exact = { equipmentId: 'body-5MiB', equipmentName: '体积边界', parts: [], padding: '' };
    exact.padding = 'x'.repeat(maximumBodyBytes - Buffer.byteLength(JSON.stringify(exact)));
    assert.equal(Buffer.byteLength(JSON.stringify(exact)), maximumBodyBytes);
    assert.equal((await post(exact)).status, 201);
    assert.deepEqual((await request('/api/equipment/body-5MiB')).json.data,
        { equipmentId: exact.equipmentId, equipmentName: exact.equipmentName, parts: [] });

    // 独立短时限HTTP实例；受控锁只在ev_test，验证真正断开SQL连接并回收事务。
    const fastStore = new EquipmentRepository({ ...config.mysql, connectionLimit: 1 }, { operationTimeoutMs: 100, closeTimeoutMs: 100 });
    const fastApp = await createApp({ ...config, operationTimeoutMs: 100, closeTimeoutMs: 200 }, { repository: fastStore });
    const fastAddress = await fastApp.start(0);
    const fastBase = `http://127.0.0.1:${fastAddress.port}`;
    const blocker = await admin.getConnection();
    try {
        const threadId = await fastStore.withConnection(async (connection) => {
            const [rows] = await connection.execute('SELECT CONNECTION_ID() AS id');
            return Number(rows[0].id);
        });
        await blocker.beginTransaction();
        await blocker.execute('INSERT INTO equipment (id,name,revision) VALUES (?,?,?)', ['lock-deadline', '受控锁', 1]);
        const started = Date.now();
        const locked = await fetch(fastBase + '/api/equipment', { method: 'POST', headers: {
            Authorization: `Bearer ${config.token}`, 'Content-Type': 'application/json' }, body: JSON.stringify(sample('lock-deadline')),
            signal: AbortSignal.timeout(2000) });
        assert.equal(locked.status, 503);
        assert.equal((await locked.json()).error.code, 'DATABASE_TIMEOUT');
        assert.ok(Date.now() - started < 1000);
        assert.equal(fastStore.pool.pool._allConnections.length, 0, 'deadline真实销毁池连接');
        await blocker.rollback();
        // 释放受控锁后查询服务器线程，证明断连事务不留下继续执行的发布。
        for (let attempt = 0; attempt < 30; ++attempt) {
            const [threads] = await admin.execute('SELECT ID FROM information_schema.PROCESSLIST WHERE ID=?', [threadId]);
            if (threads.length === 0) break;
            await new Promise((resolve) => setTimeout(resolve, 20));
        }
        const [threads] = await admin.execute('SELECT ID FROM information_schema.PROCESSLIST WHERE ID=?', [threadId]);
        assert.equal(threads.length, 0);
        const [afterLock] = await admin.execute('SELECT COUNT(*) AS n FROM equipment WHERE id=?', ['lock-deadline']);
        assert.equal(Number(afterLock[0].n), 0);
        const retry = await fetch(fastBase + '/api/equipment', { method: 'POST', headers: {
            Authorization: `Bearer ${config.token}`, 'Content-Type': 'application/json' }, body: JSON.stringify(sample('lock-deadline')) });
        assert.equal(retry.status, 201);

        // 只KILL已验证在ev_test、属于本账号的连接，模拟数据库断连，不停任何实例或主API。
        let ready;
        const readyPromise = new Promise((resolve) => { ready = resolve; });
        const disconnected = fastStore.withConnection(async (connection) => {
            const [rows] = await connection.execute('SELECT CONNECTION_ID() AS id');
            ready(Number(rows[0].id));
            await connection.execute('SELECT SLEEP(5)');
        });
        const disconnectedAssertion = assert.rejects(disconnected, { code: 'DATABASE_UNAVAILABLE' });
        const killId = await readyPromise;
        const [owned] = await admin.execute('SELECT ID FROM information_schema.PROCESSLIST WHERE ID=? AND DB=? AND USER=?',
            [killId, 'ev_test', 'ev_app']);
        assert.equal(owned.length, 1);
        await admin.query(`KILL CONNECTION ${killId}`);
        await disconnectedAssertion;
        await fastStore.health();

        // 实际持锁SQL进行中关闭应用，close销毁连接并有界完成，释放锁后不得写入半份。
        await blocker.beginTransaction();
        await blocker.execute('INSERT INTO equipment (id,name,revision) VALUES (?,?,?)', ['close-lock', '关闭锁', 1]);
        const closingPublish = fastStore.publish(sample('close-lock'));
        const closingAssertion = assert.rejects(closingPublish, { code: 'DATABASE_TIMEOUT' });
        await new Promise((resolve) => setTimeout(resolve, 20));
        const closeStarted = Date.now();
        await fastApp.close();
        await closingAssertion;
        assert.ok(Date.now() - closeStarted < 1000);
        await blocker.rollback();
        const [afterClose] = await admin.execute('SELECT COUNT(*) AS n FROM equipment WHERE id=?', ['close-lock']);
        assert.equal(Number(afterClose[0].n), 0);
    } finally {
        await blocker.rollback();
        blocker.release();
        await fastApp.close();
    }
    console.log('真实独立3310 MySQL：精确ID、UTF-16边界、并发重复、触发器回滚、重启、10000零件与5MiB通过。');
    console.log('真实ev_test：100ms锁等待超时销毁连接、受控断连恢复、持锁close有界收尾通过。');
});
