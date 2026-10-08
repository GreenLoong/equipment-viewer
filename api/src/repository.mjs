import mysql from 'mysql2/promise';
import { ApiError } from './validation.mjs';
import { annotationFingerprint } from './annotations.mjs';

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
const annotationSchema = `CREATE TABLE IF NOT EXISTS annotations (
    id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
    equipment_id VARCHAR(128) CHARACTER SET utf8mb4 COLLATE ${collation} NOT NULL,
    part_id VARCHAR(128) CHARACTER SET utf8mb4 COLLATE ${collation} NULL,
    text LONGTEXT CHARACTER SET utf8mb4 NOT NULL,
    author VARCHAR(80) CHARACTER SET utf8mb4 NOT NULL,
    updated_by VARCHAR(80) CHARACTER SET utf8mb4 NOT NULL,
    revision INT NOT NULL DEFAULT 1,
    created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    original_payload_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    KEY annotation_scope (equipment_id, updated_at, id),
    CONSTRAINT annotation_equipment FOREIGN KEY (equipment_id) REFERENCES equipment(id),
    CONSTRAINT annotation_part FOREIGN KEY (equipment_id, part_id) REFERENCES part(equipment_id, id)
) ENGINE=InnoDB`;
const annotationSelect = `SELECT id,equipment_id AS equipmentId,part_id AS partId,text,author,
    updated_by AS updatedBy,revision,UNIX_TIMESTAMP(created_at)*1000 AS createdEpoch,
    UNIX_TIMESTAMP(updated_at)*1000 AS updatedEpoch,original_payload_hash AS originalPayloadHash FROM annotations`;
function annotationDto(row) {
    return { id: row.id, equipmentId: row.equipmentId, partId: row.partId, text: row.text, author: row.author,
        updatedBy: row.updatedBy, revision: row.revision,
        createdAt: new Date(Number(row.createdEpoch)).toISOString(), updatedAt: new Date(Number(row.updatedEpoch)).toISOString() };
}

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
        this.equipmentDetailReadAttempts = 0;
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
                if (version.length !== 1 || version[0].id !== 1 || ![1, 2].includes(version[0].version))
                    throw new Error('unsupported schema version');
                // MySQL DDL隐式提交：先增建/验证，不把它伪装成可回滚迁移；仅完整兼容后升版本。
                await connection.execute(annotationSchema);
                await this.checkAnnotationSchema(connection);
                await connection.execute('UPDATE schema_version SET version=? WHERE id=? AND version IN (?,?)', [2, 1, 1, 2]);
            }, options);
        } catch (error) {
            if (error.code === 'DATABASE_TIMEOUT') throw error;
            throw new ApiError(503, 'DATABASE_SCHEMA_ERROR', '共享数据库连接或结构不兼容，请检查服务配置与迁移说明。');
        }
    }

    /** 校验0.5增量表与本库关联；失败保留已建表/旧标记供重复启动核实。 */
    async checkAnnotationSchema(connection) {
        const [columns] = await connection.execute(`SELECT COLUMN_NAME,DATA_TYPE,CHARACTER_MAXIMUM_LENGTH,COLLATION_NAME,IS_NULLABLE
            FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='annotations'`);
        const expected = { id: ['char', 36, 'ascii_bin'], equipment_id: ['varchar', 128, collation],
            part_id: ['varchar', 128, collation], text: ['longtext'], author: ['varchar', 80], updated_by: ['varchar', 80],
            revision: ['int'], created_at: ['timestamp'], updated_at: ['timestamp'], original_payload_hash: ['char', 64, 'ascii_bin'] };
        if (columns.length !== 10 || columns.some((column) => {
            const rule = expected[column.COLUMN_NAME];
            return !rule || column.DATA_TYPE !== rule[0]
                || column.IS_NULLABLE !== (column.COLUMN_NAME === 'part_id' ? 'YES' : 'NO')
                || (rule[1] !== undefined && Number(column.CHARACTER_MAXIMUM_LENGTH) !== rule[1])
                || (rule[2] !== undefined && column.COLLATION_NAME !== rule[2])
                || (['varchar', 'longtext'].includes(column.DATA_TYPE) && !column.COLLATION_NAME?.startsWith('utf8mb4_'));
        })) throw new Error('incompatible annotations');
        const [engines] = await connection.execute(`SELECT ENGINE FROM information_schema.TABLES
            WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='annotations'`);
        if (engines.length !== 1 || engines[0].ENGINE !== 'InnoDB') throw new Error('annotation engine');
        const [keys] = await connection.execute(`SELECT INDEX_NAME,NON_UNIQUE,GROUP_CONCAT(COLUMN_NAME ORDER BY SEQ_IN_INDEX) AS columns_list
            FROM information_schema.STATISTICS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='annotations'
            GROUP BY INDEX_NAME,NON_UNIQUE`);
        if (!keys.some((key) => key.INDEX_NAME === 'PRIMARY' && Number(key.NON_UNIQUE) === 0 && key.columns_list === 'id')
            || !keys.some((key) => key.INDEX_NAME === 'annotation_scope' && key.columns_list === 'equipment_id,updated_at,id'))
            throw new Error('annotation indexes');
        const [foreignKeys] = await connection.execute(`SELECT CONSTRAINT_NAME,COLUMN_NAME,REFERENCED_TABLE_SCHEMA,
            REFERENCED_TABLE_NAME,REFERENCED_COLUMN_NAME,DATABASE() AS current_schema
            FROM information_schema.KEY_COLUMN_USAGE WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='annotations'
            AND REFERENCED_TABLE_NAME IS NOT NULL`);
        const expectedForeign = [['annotation_equipment', 'equipment_id', 'equipment', 'id'],
            ['annotation_part', 'equipment_id', 'part', 'equipment_id'], ['annotation_part', 'part_id', 'part', 'id']];
        if (foreignKeys.length !== 3 || !expectedForeign.every(([constraint, column, table, referencedColumn]) => foreignKeys.some((key) =>
            key.CONSTRAINT_NAME === constraint && key.COLUMN_NAME === column && key.REFERENCED_TABLE_NAME === table
            && key.REFERENCED_COLUMN_NAME === referencedColumn && typeof key.current_schema === 'string' && key.current_schema
            && key.REFERENCED_TABLE_SCHEMA === key.current_schema))) throw new Error('annotation foreign keys');
    }

    /** 业务事务统一处理回滚与销毁，沿用总时限和取消保护。 */
    inTransaction(action, options) {
        return this.withConnection(async (connection) => {
            let transaction = false;
            try {
                await connection.beginTransaction();
                transaction = true;
                const result = await action(connection);
                await connection.commit();
                transaction = false;
                return result;
            } catch (error) {
                if (transaction && !connection.destroyed) {
                    try { await connection.rollback(); } catch { connection.destroy(); }
                }
                throw error;
            }
        }, options);
    }

    /** 核实设备/零件精确关系，不存在返回安全404，不按名称定位。 */
    async requireAnnotationScope(connection, scope) {
        const [equipment] = await connection.execute('SELECT id FROM equipment WHERE id=?', [scope.equipmentId]);
        if (equipment.length === 0) throw new ApiError(404, 'NOT_FOUND', '批注关联的设备不存在。');
        if (scope.partId !== null) {
            const [parts] = await connection.execute('SELECT id FROM part WHERE equipment_id=? AND id=?', [scope.equipmentId, scope.partId]);
            if (parts.length === 0) throw new ApiError(404, 'NOT_FOUND', '该设备的批注零件不存在。');
        }
    }

    /** scope按精确设备/可选零件身份筛选，最多最近200条，排序稳定。 */
    async listAnnotations(scope, options) {
        return this.withConnection(async (connection) => {
            await this.requireAnnotationScope(connection, scope);
            const [rows] = await connection.execute(annotationSelect + ` WHERE equipment_id=?`
                + (scope.partId === null ? '' : ' AND part_id=?') + ' ORDER BY updated_at DESC,id ASC LIMIT 200',
                scope.partId === null ? [scope.equipmentId] : [scope.equipmentId, scope.partId]);
            return rows.map(annotationDto);
        }, options);
    }

    /** UUID幂等创建，创建原始指纹独立于后续修改，重复不生成新记录。 */
    async createAnnotation(value, options) {
        const fingerprint = annotationFingerprint(value);
        const existingResult = (row) => {
            if (row.originalPayloadHash !== fingerprint)
                throw new ApiError(409, 'IDEMPOTENCY_CONFLICT', '该requestId已用于其他批注内容，请核实原保存结果。');
            return { created: false, annotation: annotationDto(row) };
        };
        return this.inTransaction(async (connection) => {
            const [existing] = await connection.execute(annotationSelect + ' WHERE id=?', [value.requestId]);
            if (existing.length > 0) return existingResult(existing[0]);
            await this.requireAnnotationScope(connection, value);
            try {
                await connection.execute(`INSERT INTO annotations (id,equipment_id,part_id,text,author,updated_by,original_payload_hash,
                    revision,created_at,updated_at) VALUES (?,?,?,?,?,?,?,1,CURRENT_TIMESTAMP(6),CURRENT_TIMESTAMP(6))`,
                    [value.requestId, value.equipmentId, value.partId, value.text, value.author, value.author, fingerprint]);
            } catch (error) {
                if (error.code !== 'ER_DUP_ENTRY') throw error;
                // 并发同UUID插入在唯一键等待后失败；锁定读越过此前快照，读取已提交原记录。
                const [duplicate] = await connection.execute(annotationSelect + ' WHERE id=? FOR UPDATE', [value.requestId]);
                if (duplicate.length !== 1) throw error;
                return existingResult(duplicate[0]);
            }
            const [rows] = await connection.execute(annotationSelect + ' WHERE id=?', [value.requestId]);
            return { created: true, annotation: annotationDto(rows[0]) };
        }, options);
    }

    /** 单条CAS不覆盖他人修改，返回DTO在commit前读取，提交后不再依赖额外查询。 */
    async updateAnnotation(id, value, options) {
        return this.inTransaction(async (connection) => {
            const [result] = await connection.execute(`UPDATE annotations SET text=?,updated_by=?,revision=revision+1,
                updated_at=CURRENT_TIMESTAMP(6) WHERE id=? AND revision=?`, [value.text, value.editor, id, value.expectedRevision]);
            const [rows] = await connection.execute(annotationSelect + ' WHERE id=? FOR UPDATE', [id]);
            if (rows.length === 0) throw new ApiError(404, 'NOT_FOUND', '批注不存在。');
            if (result.affectedRows !== 1)
                throw new ApiError(409, 'REVISION_CONFLICT', '批注已由他人修改，请刷新已保存内容，确认草稿后显式提交。');
            return annotationDto(rows[0]);
        }, options);
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
        this.equipmentDetailReadAttempts += 1; // 调用尝试即计数，失败同样计入，不以MISS数量猜测数据库访问。
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
