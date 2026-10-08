/** 0.4真实结构固定测试夹具；仅在调用者先确认空ev_test后执行。 */
export const v1Schema = [
    `CREATE TABLE equipment (id VARCHAR(128) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_bin NOT NULL PRIMARY KEY,
        name LONGTEXT CHARACTER SET utf8mb4 NOT NULL, revision INT NOT NULL DEFAULT 1,
        created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)) ENGINE=InnoDB`,
    `CREATE TABLE part (equipment_id VARCHAR(128) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_bin NOT NULL,
        id VARCHAR(128) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_bin NOT NULL, ordinal INT NOT NULL,
        name LONGTEXT CHARACTER SET utf8mb4 NOT NULL, material LONGTEXT CHARACTER SET utf8mb4 NOT NULL,
        shape VARCHAR(8) CHARACTER SET utf8mb4 NOT NULL, dimensions JSON NOT NULL, position JSON NOT NULL,
        PRIMARY KEY(equipment_id,id), UNIQUE KEY part_ordinal(equipment_id,ordinal),
        CONSTRAINT part_equipment FOREIGN KEY(equipment_id) REFERENCES equipment(id)) ENGINE=InnoDB`,
    `CREATE TABLE schema_version (id TINYINT NOT NULL PRIMARY KEY,version INT NOT NULL) ENGINE=InnoDB`
];
