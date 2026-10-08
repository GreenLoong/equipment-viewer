import mysql from 'mysql2/promise';
import { ApiError } from './validation.mjs';

const databaseError = () => new ApiError(503, 'DATABASE_UNAVAILABLE', '共享数据库暂时不可用，请稍后重试。');
const timeoutError = () => new ApiError(503, 'DATABASE_TIMEOUT', '共享数据库操作超时，请稍后重试。');
const collation = 'utf8mb4_0900_bin';
const schemaStatements = [
    `CREATE TABLE IF NOT EXISTS equipment (
        id VARCHAR(128) CHARACTER SET utf8mb4 COLLATE ${collation} NOT NULL PRIMARY KEY,
        name LONGTEXT CHARACTER SET utf8mb4 NOT NULL,
        revision INT NOT NULL DEFAULT 1,
        created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
    ) ENGINE=InnoDB`,
    `CREATE TABLE IF NOT EXISTS part (
        equipment_id VARCHAR(128) CHARACTER SET utf8mb4 COLLATE ${collation} NOT NULL,
        id VARCHAR(128) CHARACTER SET utf8mb4 COLLATE ${collation} NOT NULL,
        ordinal INT NOT NULL,
        name LONGTEXT CHARACTER SET utf8mb4 NOT NULL,
        material LONGTEXT CHARACTER SET utf8mb4 NOT NULL,
        shape VARCHAR(8) CHARACTER SET utf8mb4 NOT NULL,
        dimensions JSON NOT NULL, position JSON NOT NULL,
        PRIMARY KEY (equipment_id, id),
        UNIQUE KEY part_ordinal (equipment_id, ordinal),
        CONSTRAINT part_equipment FOREIGN KEY (equipment_id) REFERENCES equipment(id)
    ) ENGINE=InnoDB`,
    `CREATE TABLE IF NOT EXISTS schema_version (id TINYINT NOT NULL PRIMARY KEY, version INT NOT NULL) ENGINE=InnoDB`
];

/** 连接池与参数绑定的数据层；所有发布写入在一个事务内完成。 */
export class EquipmentRepository {
    constructor(config, { pool, operationTimeoutMs = 10000, closeTimeoutMs = 1000 } = {}) {
        this.pool = pool ?? mysql.createPool({ ...config, charset: 'utf8mb4', waitForConnections: true,
            queueLimit: 32, connectTimeout: 5000, multipleStatements: false });
        this.operationTimeoutMs = operationTimeoutMs;
        this.closeTimeoutMs = closeTimeoutMs;
        this.active = new Set();
        this.closed = false;
        this.closing = null;
    }

    /** 总时限包含排队/全部SQL/事务收尾；超时销毁实际连接，晚到的连接也不执行SQL。 */
    withConnection(action, { signal } = {}) {
        if (this.closed) return Promise.reject(databaseError());
        return new Promise((resolve, reject) => {
            let connection;
            let settled = false;
            let destroyed = false;
            const destroy = () => {
                if (connection && !destroyed) { destroyed = true; connection.destroy(); }
            };
            const release = () => {
                if (connection && !destroyed) {
                    try { connection.release(); } catch { destroy(); }
                }
            };
            const cleanup = () => {
                clearTimeout(timer);
                signal?.removeEventListener('abort', abort);
                this.active.delete(abort);
            };
            const abort = () => {
                if (settled) return;
                settled = true;
                destroy();
                cleanup();
                reject(timeoutError());
            };
            const timer = setTimeout(abort, this.operationTimeoutMs);
            this.active.add(abort);
            signal?.addEventListener('abort', abort, { once: true });
            if (signal?.aborted) { abort(); return; }
            const check = () => { if (settled || destroyed || this.closed) throw timeoutError(); };
            const invoke = async (method, ...args) => {
                check();
                const result = await connection[method](...args);
                check();
                return result;
            };
            (async () => {
                try {
                    connection = await this.pool.getConnection();
                    if (settled || this.closed) { destroy(); return; }
                    const session = { execute: (...args) => invoke('execute', ...args),
                        beginTransaction: () => invoke('beginTransaction'), commit: () => invoke('commit'),
                        rollback: () => invoke('rollback'), destroy,
                        get destroyed() { return destroyed; } };
                    const result = await action(session);
                    if (settled) return;
                    settled = true;
                    cleanup();
                    release();
                    resolve(result);
                } catch (error) {
                    if (settled) return;
                    settled = true;
                    cleanup();
                    if (!(error instanceof ApiError)) destroy();
                    release();
                    reject(error instanceof ApiError ? error : databaseError());
                }
            })();
        });
    }

    /** 只增建缺失表，校验现有结构；不创建数据库、不清理旧表。 */
    async initialize(options) {
        try {
            await this.withConnection(async (connection) => {
                for (const statement of schemaStatements) await connection.execute(statement);
                const [columns] = await connection.execute(`SELECT TABLE_NAME, COLUMN_NAME, DATA_TYPE,
                    CHARACTER_MAXIMUM_LENGTH, COLLATION_NAME, IS_NULLABLE
                    FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE()
                    AND TABLE_NAME IN ('equipment','part','schema_version')`);
                const expected = {
                    equipment: { id: ['varchar', 128, collation], name: ['longtext'], revision: ['int'], created_at: ['timestamp'] },
                    part: { equipment_id: ['varchar', 128, collation], id: ['varchar', 128, collation],
                        ordinal: ['int'], name: ['longtext'], material: ['longtext'], shape: ['varchar', 8], dimensions: ['json'], position: ['json'] },
                    schema_version: { id: ['tinyint'], version: ['int'] }
                };
                if (columns.length !== 14 || columns.some((column) => {
                    const rule = expected[column.TABLE_NAME]?.[column.COLUMN_NAME];
                    return !rule || column.DATA_TYPE !== rule[0] || column.IS_NULLABLE !== 'NO'
                        || (rule[1] !== undefined && Number(column.CHARACTER_MAXIMUM_LENGTH) !== rule[1])
                        || (rule[2] !== undefined && column.COLLATION_NAME !== rule[2])
                        || (['longtext', 'varchar'].includes(column.DATA_TYPE)
                            && !column.COLLATION_NAME?.startsWith('utf8mb4_'));
                })) throw new Error('incompatible schema');
                const [engines] = await connection.execute(`SELECT ENGINE FROM information_schema.TABLES
                    WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN ('equipment','part','schema_version')`);
                if (engines.length !== 3 || engines.some((item) => item.ENGINE !== 'InnoDB'))
                    throw new Error('transactional engine required');
                const [keys] = await connection.execute(`SELECT TABLE_NAME, INDEX_NAME, NON_UNIQUE,
                    GROUP_CONCAT(COLUMN_NAME ORDER BY SEQ_IN_INDEX) AS columns_list
                    FROM information_schema.STATISTICS WHERE TABLE_SCHEMA=DATABASE()
                    AND TABLE_NAME IN ('equipment','part','schema_version') GROUP BY TABLE_NAME,INDEX_NAME,NON_UNIQUE`);
                for (const [table, name, list] of [['equipment','PRIMARY','id'], ['part','PRIMARY','equipment_id,id'],
                    ['part','part_ordinal','equipment_id,ordinal'], ['schema_version','PRIMARY','id']]) {
                    if (!keys.some((key) => key.TABLE_NAME === table && key.INDEX_NAME === name
                        && Number(key.NON_UNIQUE) === 0 && key.columns_list === list)) throw new Error('missing identity index');
                }
                const [foreignKeys] = await connection.execute(`SELECT COLUMN_NAME, REFERENCED_TABLE_NAME, REFERENCED_COLUMN_NAME,
                    REFERENCED_TABLE_SCHEMA, DATABASE() AS current_schema
                    FROM information_schema.KEY_COLUMN_USAGE WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='part'
                    AND REFERENCED_TABLE_NAME IS NOT NULL`);
                if (foreignKeys.length !== 1 || foreignKeys[0].COLUMN_NAME !== 'equipment_id'
                    || typeof foreignKeys[0].current_schema !== 'string' || !foreignKeys[0].current_schema
                    || foreignKeys[0].REFERENCED_TABLE_SCHEMA !== foreignKeys[0].current_schema
                    || foreignKeys[0].REFERENCED_TABLE_NAME !== 'equipment' || foreignKeys[0].REFERENCED_COLUMN_NAME !== 'id')
                    throw new Error('missing foreign key');
                const [pad] = await connection.execute('SELECT PAD_ATTRIBUTE FROM information_schema.COLLATIONS WHERE COLLATION_NAME=?', [collation]);
                if (pad[0]?.PAD_ATTRIBUTE !== 'NO PAD') throw new Error('identity padding unsupported');
                await connection.execute('INSERT IGNORE INTO schema_version (id,version) VALUES (?,?)', [1, 1]);
                const [version] = await connection.execute('SELECT id,version FROM schema_version');
                if (version.length !== 1 || version[0].id !== 1 || version[0].version !== 1)
                    throw new Error('unsupported schema version');
            }, options);
        } catch (error) {
            if (error.code === 'DATABASE_TIMEOUT') throw error;
            throw new ApiError(503, 'DATABASE_SCHEMA_ERROR', '共享数据库连接或结构不兼容，请检查服务配置与迁移说明。');
        }
    }

    async health(options) {
        await this.withConnection((connection) => connection.execute('SELECT 1'), options);
    }

    /** 返回按精确ID排序的设备摘要。 */
    async list(options) {
        return this.withConnection(async (connection) => {
            const [rows] = await connection.execute(`SELECT e.id,e.name,e.revision,COUNT(p.id) AS partCount
                FROM equipment e LEFT JOIN part p ON p.equipment_id=e.id
                GROUP BY e.id,e.name,e.revision ORDER BY e.id`);
            return rows.map((row) => ({ ...row, partCount: Number(row.partCount) }));
        }, options);
    }

    /** 按设备内顺序还原可导入JSON；不存在返回null。 */
    async get(id, options) {
        return this.withConnection(async (connection) => {
            const [equipment] = await connection.execute('SELECT id,name FROM equipment WHERE id=?', [id]);
            if (equipment.length === 0) return null;
            const [rows] = await connection.execute(`SELECT id,name,material,shape,dimensions,position
                FROM part WHERE equipment_id=? ORDER BY ordinal`, [id]);
            return { equipmentId: equipment[0].id, equipmentName: equipment[0].name,
                parts: rows.map((row) => ({ ...row,
                    dimensions: typeof row.dimensions === 'string' ? JSON.parse(row.dimensions) : row.dimensions,
                    position: typeof row.position === 'string' ? JSON.parse(row.position) : row.position })) };
        }, options);
    }

    /** 提交后直接返回已知摘要，避免后续读失败误报发布失败。 */
    async publish(equipment, options) {
        return this.withConnection(async (connection) => {
            let transaction = false;
            try {
                await connection.beginTransaction();
                transaction = true;
                await connection.execute('INSERT INTO equipment (id,name,revision) VALUES (?,?,?)',
                    [equipment.equipmentId, equipment.equipmentName, 1]);
                for (const [ordinal, part] of equipment.parts.entries()) {
                    await connection.execute(`INSERT INTO part (equipment_id,id,ordinal,name,material,shape,dimensions,position)
                        VALUES (?,?,?,?,?,?,?,?)`, [equipment.equipmentId, part.id, ordinal, part.name, part.material,
                        part.shape, JSON.stringify(part.dimensions), JSON.stringify(part.position)]);
                }
                await connection.commit();
                transaction = false;
                return { id: equipment.equipmentId, name: equipment.equipmentName, partCount: equipment.parts.length, revision: 1 };
            } catch (error) {
                if (transaction && !connection.destroyed) {
                    try { await connection.rollback(); }
                    catch { connection.destroy(); /* 无法证明回滚成功的连接不可回池。 */ }
                }
                if (error instanceof ApiError) throw error;
                if (error.code === 'ER_DUP_ENTRY') throw new ApiError(409, 'EQUIPMENT_EXISTS', '设备ID已存在，不能覆盖。');
                throw databaseError();
            }
        }, options);
    }

    /** 关闭时取消活动请求并销毁连接，池的握手/QUIT也有有界收尾。 */
    async close() {
        if (this.closing) return this.closing;
        this.closed = true;
        for (const abort of [...this.active]) abort();
        this.closing = new Promise((resolve) => {
            const finish = () => { clearTimeout(timer); resolve(); };
            const destroyRemaining = () => {
                // mysql2公开end在握手或QUIT卡住时无期限；固定依赖内部队列用于最后销毁实际socket。
                const connections = this.pool.pool?._allConnections;
                if (!connections) return;
                // destroy会移除池内成员，先快照以确保每个剩余连接都得到收尾。
                const remaining = Array.from({ length: connections.length }, (_, index) => connections.get(index));
                for (const connection of remaining) {
                    try { connection?.destroy(); } catch { /* 单个收尾失败不能跳过其余socket。 */ }
                }
            };
            const failOrTimeout = () => {
                destroyRemaining();
                finish();
            };
            const timer = setTimeout(failOrTimeout, this.closeTimeoutMs);
            Promise.resolve().then(() => this.pool.end()).then(finish, failOrTimeout);
        });
        return this.closing;
    }
}
