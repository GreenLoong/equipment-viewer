import test from 'node:test';
import assert from 'node:assert/strict';
import mysql from 'mysql2/promise';
import { createApp, loadConfig } from '../src/server.mjs';
import { maximumBodyBytes } from '../src/validation.mjs';
import { EquipmentRepository } from '../src/repository.mjs';
import { randomUUID } from 'node:crypto';
import { v1Schema } from './v1-schema-fixture.mjs';

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
                await admin.execute('DROP TABLE IF EXISTS annotations');
                await admin.execute('DROP TABLE IF EXISTS part');
                await admin.execute('DROP TABLE IF EXISTS equipment');
                await admin.execute('DROP TABLE IF EXISTS schema_version');
            }
        } finally { await admin.end(); }
    });
    const [existing] = await admin.execute('SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE()');
    assert.equal(existing.length, 0, 'ev_test非空时拒绝覆盖或清理旧表，请检查上次测试交接');
    ownTables = true;
    // 从已有0.4真实表和几何升级，证明迁移不会只在空新库通过。
    for (const statement of v1Schema) await admin.execute(statement);
    await admin.execute('INSERT INTO schema_version (id,version) VALUES (?,?)', [1, 1]);
    const legacy = sample('legacy-v1');
    await admin.execute('INSERT INTO equipment (id,name,revision) VALUES (?,?,?)', [legacy.equipmentId, legacy.equipmentName, 1]);
    for (const [ordinal, part] of legacy.parts.entries()) await admin.execute(`INSERT INTO part
        (equipment_id,id,ordinal,name,material,shape,dimensions,position) VALUES (?,?,?,?,?,?,?,?)`,
        [legacy.equipmentId, part.id, ordinal, part.name, part.material, part.shape, JSON.stringify(part.dimensions), JSON.stringify(part.position)]);
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
    assert.deepEqual((await request('/api/equipment?id=legacy-v1')).json.data, legacy);
    const [migrated] = await admin.execute('SELECT version FROM schema_version WHERE id=?', [1]);
    assert.equal(migrated[0].version, 2);
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
    assert.equal(list.length, 10);
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

    const annotationPost = (value) => request('/api/annotations', { method: 'POST',
        headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(value) });
    const annotationPut = (id, value) => request('/api/annotations/' + id, { method: 'PUT',
        headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(value) });
    const scope = (equipmentId, partId) => '/api/annotations?equipmentId=' + encodeURIComponent(equipmentId)
        + (partId === undefined ? '' : '&partId=' + encodeURIComponent(partId));
    const original = { requestId: randomUUID().toUpperCase(), equipmentId: 'a', partId: null,
        text: "工艺 <b>纯文本</b>' 原稿", author: '创建者甲' };
    const created = await annotationPost(original);
    assert.equal(created.status, 201);
    const annotation = created.json.data;
    assert.equal(annotation.id, original.requestId.toLowerCase());
    assert.equal(annotation.revision, 1);
    assert.equal(annotation.author, '创建者甲');
    assert.equal(annotation.updatedBy, '创建者甲');
    assert.equal(annotation.partId, null);
    assert.match(annotation.createdAt, /^\d{4}-\d{2}-\d{2}T.*Z$/);
    const [epoch] = await admin.execute('SELECT UNIX_TIMESTAMP(created_at)*1000 AS epoch FROM annotations WHERE id=?', [annotation.id]);
    assert.equal(Date.parse(annotation.createdAt), Math.trunc(Number(epoch[0].epoch)), 'UTC DTO来自MySQL epoch，不用本机日期字符串解析');
    const retryOriginal = await annotationPost(original);
    assert.equal(retryOriginal.status, 200);
    assert.deepEqual(retryOriginal.json.data, annotation);
    assert.equal((await annotationPost({ ...original, text: '不同原稿' })).json.error.code, 'IDEMPOTENCY_CONFLICT');
    const concurrentIdem = { ...original, requestId: randomUUID(), text: '并发重复创建' };
    const duplicateAnnotations = await Promise.all([annotationPost(concurrentIdem), annotationPost(concurrentIdem)]);
    assert.deepEqual(duplicateAnnotations.map((item) => item.status).sort(), [200, 201]);
    assert.equal(duplicateAnnotations[0].json.data.id, duplicateAnnotations[1].json.data.id);

    for (const partId of ['a', 'A', 'a ']) {
        const saved = await annotationPost({ ...original, requestId: randomUUID(), partId, text: `零件工艺 ${partId}` });
        assert.equal(saved.status, 201);
        const filtered = await request(scope('a', partId));
        assert.equal(filtered.status, 200);
        assert.equal(filtered.json.data.length, 1);
        assert.equal(filtered.json.data[0].partId, partId);
    }
    const anotherEquipment = await annotationPost({ ...original, requestId: randomUUID(), equipmentId: 'A', partId: 'a' });
    assert.equal(anotherEquipment.status, 201);
    assert.equal((await request(scope('A', 'a'))).json.data.length, 1);
    assert.ok((await request(scope('a'))).json.data.every((item) => item.equipmentId === 'a'));
    assert.equal((await request(scope('missing'))).status, 404);
    assert.equal((await request(scope('a', 'missing'))).status, 404);
    assert.equal((await annotationPost({ ...original, requestId: randomUUID(), equipmentId: 'missing' })).status, 404);
    assert.equal((await annotationPost({ ...original, requestId: randomUUID(), partId: 'missing' })).status, 404);
    assert.equal((await annotationPost({ ...original, requestId: randomUUID(), text: ' ' })).status, 400);
    assert.equal((await annotationPost({ ...original, requestId: randomUUID(), text: 'x'.repeat(2001) })).status, 400);
    assert.equal((await request('/api/annotations?equipmentId=a&equipmentId=A')).status, 400);
    assert.equal((await request('/api/annotations?equipmentId=%FF')).status, 400);
    assert.equal((await request('/api/annotations?equipmentId=a&unknown=x')).status, 400);
    const limitText = await annotationPost({ ...original, requestId: randomUUID(), text: '😀'.repeat(1000), author: '😀'.repeat(40) });
    assert.equal(limitText.status, 201);
    assert.equal(limitText.json.data.text.length, 2000);
    assert.equal(limitText.json.data.author.length, 80);

    // 不仅API预检，数据库联合FK也独立拒绝孤立零件关联。
    await assert.rejects(admin.execute(`INSERT INTO annotations
        (id,equipment_id,part_id,text,author,updated_by,original_payload_hash) VALUES (?,?,?,?,?,?,?)`,
        [randomUUID(), 'a', 'missing', '非法外键', 'test', 'test', '0'.repeat(64)]), { code: 'ER_NO_REFERENCED_ROW_2' });
    const concurrentUpdates = await Promise.all([
        annotationPut(annotation.id, { text: '修改者乙草稿', editor: '编辑者乙', expectedRevision: 1 }),
        annotationPut(annotation.id, { text: '修改者丙草稿', editor: '编辑者丙', expectedRevision: 1 })
    ]);
    assert.deepEqual(concurrentUpdates.map((item) => item.status).sort(), [200, 409]);
    const updated = concurrentUpdates.find((item) => item.status === 200).json.data;
    const conflict = concurrentUpdates.find((item) => item.status === 409);
    assert.equal(conflict.json.error.code, 'REVISION_CONFLICT');
    assert.equal(updated.revision, 2);
    assert.equal(updated.author, annotation.author);
    assert.equal(updated.createdAt, annotation.createdAt);
    assert.equal(updated.equipmentId, annotation.equipmentId);
    assert.ok(['编辑者乙', '编辑者丙'].includes(updated.updatedBy));
    assert.ok(Date.parse(updated.updatedAt) >= Date.parse(updated.createdAt));
    assert.equal((await annotationPut(randomUUID(), { text: '不存在', editor: '甲', expectedRevision: 1 })).status, 404);
    assert.equal((await annotationPut(annotation.id, { text: '非法版本', editor: '甲', expectedRevision: 0 })).status, 400);
    const retryAfterEdit = await annotationPost(original);
    assert.equal(retryAfterEdit.status, 200);
    assert.deepEqual(retryAfterEdit.json.data, updated, '修改不改变原始创建指纹，重复请求返回现存记录');
    const explicitSubmit = await annotationPut(annotation.id, { text: '已刷新后显式重新提交', editor: '编辑者丙', expectedRevision: 2 });
    assert.equal(explicitSubmit.status, 200);
    assert.equal(explicitSubmit.json.data.revision, 3);
    assert.equal(explicitSubmit.json.data.author, annotation.author);

    // 模拟DDL已提交但版本标记尚未升阶：不删表，重复启动兼容校验后继续升标记。
    await app.close();
    await admin.execute('UPDATE schema_version SET version=? WHERE id=?', [1, 1]);
    app = await createApp(config);
    address = await app.start(0);
    base = `http://127.0.0.1:${address.port}`;
    const persisted = (await request(scope('a'))).json.data.find((item) => item.id === annotation.id);
    assert.deepEqual(persisted, explicitSubmit.json.data);
    assert.deepEqual((await request('/api/equipment?id=legacy-v1')).json.data, legacy);
    const [recoveredVersion] = await admin.execute('SELECT version FROM schema_version WHERE id=?', [1]);
    assert.equal(recoveredVersion[0].version, 2);

    assert.equal((await post({ equipmentId: 'annotation-limit', equipmentName: '最近200测试', parts: [] })).status, 201);
    const values = [];
    for (let index = 0; index < 205; ++index) values.push(randomUUID(), 'annotation-limit', null, `记录${index}`, '测试', '测试', '0'.repeat(64));
    await admin.execute(`INSERT INTO annotations (id,equipment_id,part_id,text,author,updated_by,original_payload_hash) VALUES `
        + Array(205).fill('(?,?,?,?,?,?,?)').join(','), values);
    const recent = (await request(scope('annotation-limit'))).json.data;
    assert.equal(recent.length, 200);
    const [ordered] = await admin.execute('SELECT id FROM annotations WHERE equipment_id=? ORDER BY updated_at DESC,id ASC LIMIT 200', ['annotation-limit']);
    assert.deepEqual(recent.map((item) => item.id), ordered.map((item) => item.id));
    assert.deepEqual((await request('/api/equipment?id=a')).json.data, sample('a'), '批注不编辑geometry');

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
    console.log('真实0.5：v1无损迁移/DDL重入、幂等并发、精确关联FK、CAS 200+409、作者/UTC、重建持久与最近200通过。');
});
