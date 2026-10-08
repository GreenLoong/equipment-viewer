import test from 'node:test';
import assert from 'node:assert/strict';
import { EquipmentRepository } from '../src/repository.mjs';

const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const equipment = { equipmentId: 'fixture', equipmentName: '测试', parts: [] };
function connection(overrides = {}) {
    return { released: false, destroyed: false, execute: async () => [[]],
        beginTransaction: async () => {}, commit: async () => {}, rollback: async () => {},
        release() { this.released = true; }, destroy() { this.destroyed = true; }, ...overrides };
}
function repository(conn, options = {}) {
    return new EquipmentRepository({}, { pool: { getConnection: async () => conn, end: async () => {} },
        operationTimeoutMs: 50, closeTimeoutMs: 20, ...options });
}

test('插入失败且rollback失败必须destroy，不能release回池', async () => {
    const conn = connection({ execute: async () => { throw new Error('insert failure'); },
        rollback: async () => { throw new Error('rollback failure'); } });
    await assert.rejects(repository(conn).publish(equipment), { code: 'DATABASE_UNAVAILABLE' });
    assert.equal(conn.destroyed, true);
    assert.equal(conn.released, false);
});

test('SQL、commit、rollback挂起都在总时限销毁真实连接，不仅返回超时', async () => {
    for (const stage of ['execute', 'commit', 'rollback']) {
        let interrupted;
        const conn = connection({ destroy() { this.destroyed = true; interrupted?.(new Error('destroy interrupted')); } });
        conn[stage] = () => new Promise((_resolve, reject) => { interrupted = reject; });
        if (stage === 'rollback') conn.execute = async () => { throw new Error('insert failure'); };
        const store = repository(conn);
        const started = Date.now();
        await assert.rejects(stage === 'execute' ? store.health() : store.publish(equipment), { code: 'DATABASE_TIMEOUT' });
        assert.ok(Date.now() - started < 500);
        assert.equal(conn.destroyed, true);
        assert.equal(conn.released, false);
        await store.close();
    }
});

test('排队获取连接到期后晚到连接销毁且不执行SQL', async () => {
    let supply;
    let executed = false;
    const conn = connection({ execute: async () => { executed = true; return [[]]; } });
    const store = new EquipmentRepository({}, { pool: { getConnection: () => new Promise((resolve) => { supply = resolve; }),
        end: async () => {} }, operationTimeoutMs: 20 });
    await assert.rejects(store.health(), { code: 'DATABASE_TIMEOUT' });
    supply(conn);
    await wait(0);
    assert.equal(conn.destroyed, true);
    assert.equal(conn.released, false);
    assert.equal(executed, false);
    await store.close();
});

test('close立即取消活动SQL；池end卡住也有界销毁握手/空闲socket', async () => {
    let interrupted;
    const conn = connection({ execute: () => new Promise((_resolve, reject) => { interrupted = reject; }),
        destroy() { this.destroyed = true; interrupted?.(new Error('closed')); } });
    const pendingSocket = { destroyed: false, destroy() { this.destroyed = true; } };
    const store = new EquipmentRepository({}, { pool: { getConnection: async () => conn,
        end: () => new Promise(() => {}), pool: { _allConnections: { length: 1, get: () => pendingSocket } } },
        operationTimeoutMs: 5000, closeTimeoutMs: 20 });
    const running = store.health();
    const assertion = assert.rejects(running, { code: 'DATABASE_TIMEOUT' });
    await wait(0);
    const started = Date.now();
    await store.close();
    await assertion;
    assert.ok(Date.now() - started < 500);
    assert.equal(conn.destroyed, true);
    assert.equal(pendingSocket.destroyed, true);
    assert.equal(conn.released, false);
    await store.close();
    await assert.rejects(store.health(), { code: 'DATABASE_UNAVAILABLE' });
});

test('同名equipment外键若指向另一schema必须拒绝，纯元数据不触其他数据库', async () => {
    const tables = { equipment: { id: ['varchar', 128], name: ['longtext'], revision: ['int'], created_at: ['timestamp'] },
        part: { equipment_id: ['varchar', 128], id: ['varchar', 128], ordinal: ['int'], name: ['longtext'], material: ['longtext'],
            shape: ['varchar', 8], dimensions: ['json'], position: ['json'] }, schema_version: { id: ['tinyint'], version: ['int'] } };
    const columns = Object.entries(tables).flatMap(([table, fields]) => Object.entries(fields).map(([name, rule]) => ({
        TABLE_NAME: table, COLUMN_NAME: name, DATA_TYPE: rule[0], CHARACTER_MAXIMUM_LENGTH: rule[1],
        COLLATION_NAME: ['varchar', 'longtext'].includes(rule[0]) ? 'utf8mb4_0900_bin' : null, IS_NULLABLE: 'NO' })));
    let foreignRead = false;
    const conn = connection({ execute: async (sql) => {
        if (sql.includes('information_schema.COLUMNS')) return [columns];
        if (sql.includes('information_schema.TABLES')) return [[{ ENGINE: 'InnoDB' }, { ENGINE: 'InnoDB' }, { ENGINE: 'InnoDB' }]];
        if (sql.includes('information_schema.STATISTICS')) return [[
            ['equipment', 'PRIMARY', 'id'], ['part', 'PRIMARY', 'equipment_id,id'], ['part', 'part_ordinal', 'equipment_id,ordinal'],
            ['schema_version', 'PRIMARY', 'id']
        ].map(([table, name, list]) => ({ TABLE_NAME: table, INDEX_NAME: name, columns_list: list, NON_UNIQUE: 0 }))];
        if (sql.includes('information_schema.KEY_COLUMN_USAGE')) {
            foreignRead = true;
            assert.ok(sql.includes('REFERENCED_TABLE_SCHEMA'));
            return [[{ COLUMN_NAME: 'equipment_id', REFERENCED_TABLE_NAME: 'equipment', REFERENCED_COLUMN_NAME: 'id',
                REFERENCED_TABLE_SCHEMA: 'other_schema', current_schema: 'ev_test' }]];
        }
        return [[]];
    } });
    await assert.rejects(repository(conn, { operationTimeoutMs: 1000 }).initialize(), { code: 'DATABASE_SCHEMA_ERROR' });
    assert.equal(foreignRead, true);
});

test('pool.end拒绝时也销毁全部剩余socket，不能清看门狗后直接完成', async () => {
    const sockets = [connection(), connection()];
    const store = new EquipmentRepository({}, { pool: { end: async () => { throw new Error('first QUIT failed'); },
        pool: { _allConnections: { get length() { return sockets.length; }, get: (index) => sockets[index] } } },
        closeTimeoutMs: 1000 });
    const originalSockets = [...sockets];
    for (const socket of originalSockets) socket.destroy = () => {
        socket.destroyed = true;
        sockets.splice(sockets.indexOf(socket), 1);
    };
    const started = Date.now();
    await store.close();
    assert.ok(Date.now() - started < 500, '拒绝路径即时收尾，不等待看门狗');
    assert.ok(originalSockets.every((socket) => socket.destroyed));
    assert.equal(sockets.length, 0);
    await store.close();
});
