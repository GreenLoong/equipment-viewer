#include "equipmentrepository.h"
#include "numericutil.h"
#include "partvalidation.h"

#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QRegularExpression>
#include <QtEndian>
#include <cmath>

namespace {
// 与 QString::trimmed 的空白语义一致，SQL 也拒绝仅有制表符或全角空格的文本。
QString nonBlank(const QString &column)
{
    return QStringLiteral("length(trim(%1, char(9,10,11,12,13,32,133,160,5760,"
                          "8192,8193,8194,8195,8196,8197,8198,8199,8200,8201,8202,"
                          "8232,8233,8239,8287,12288))) > 0").arg(column);
}

QString equipmentSchema()
{
    return QStringLiteral("CREATE TABLE equipment ("
                          "id TEXT NOT NULL PRIMARY KEY CHECK(%1), "
                          "name TEXT NOT NULL CHECK(%2)) STRICT")
        .arg(nonBlank(QStringLiteral("id")), nonBlank(QStringLiteral("name")));
}

QString legacyPartSchema()
{
    return QStringLiteral(
        "CREATE TABLE part ("
        "equipment_id TEXT NOT NULL CHECK(%1), "
        "id TEXT NOT NULL CHECK(%2), "
        "name TEXT NOT NULL CHECK(%3), "
        "material TEXT NOT NULL CHECK(%4), "
        "shape TEXT NOT NULL CHECK(shape IN ('box','cylinder')), "
        "position_x REAL NOT NULL CHECK(abs(position_x) <= 1.7976931348623157e308), "
        "position_y REAL NOT NULL CHECK(abs(position_y) <= 1.7976931348623157e308), "
        "position_z REAL NOT NULL CHECK(abs(position_z) <= 1.7976931348623157e308), "
        "size_x REAL, size_y REAL, size_z REAL, radius REAL, length REAL, "
        "PRIMARY KEY(equipment_id,id), "
        "FOREIGN KEY(equipment_id) REFERENCES equipment(id), "
        // CHECK 遇到 NULL 本来会放行，所以正尺寸还必须显式检查 IS NOT NULL。
        "CHECK((shape = 'box' AND size_x IS NOT NULL AND size_y IS NOT NULL "
        "AND size_z IS NOT NULL AND size_x > 0 AND size_y > 0 AND size_z > 0 "
        "AND size_x <= 1.7976931348623157e308 AND size_y <= 1.7976931348623157e308 "
        "AND size_z <= 1.7976931348623157e308 AND radius IS NULL AND length IS NULL) "
        "OR (shape = 'cylinder' AND radius IS NOT NULL AND length IS NOT NULL "
        "AND radius > 0 AND length > 0 AND radius <= 1.7976931348623157e308 "
        "AND length <= 1.7976931348623157e308 "
        "AND size_x IS NULL AND size_y IS NULL AND size_z IS NULL))) STRICT")
        .arg(nonBlank(QStringLiteral("equipment_id")), nonBlank(QStringLiteral("id")),
             nonBlank(QStringLiteral("name")), nonBlank(QStringLiteral("material")));
}

QString partSchema()
{
    QString sql = legacyPartSchema();
    sql.replace("shape IN ('box','cylinder')", "shape IN ('box','cylinder','mesh')");
    sql.replace("radius REAL, length REAL, ",
                "radius REAL, length REAL, mesh_blob BLOB, cad_source_name TEXT, cad_source_sha256 TEXT, ");
    sql.replace("AND radius IS NULL AND length IS NULL) ",
                "AND radius IS NULL AND length IS NULL AND mesh_blob IS NULL "
                "AND cad_source_name IS NULL AND cad_source_sha256 IS NULL) ");
    sql.replace("AND size_x IS NULL AND size_y IS NULL AND size_z IS NULL))) STRICT",
        "AND size_x IS NULL AND size_y IS NULL AND size_z IS NULL AND mesh_blob IS NULL "
        "AND cad_source_name IS NULL AND cad_source_sha256 IS NULL) "
        "OR (shape = 'mesh' AND size_x IS NOT NULL AND size_y IS NOT NULL AND size_z IS NOT NULL "
        "AND size_x > 0 AND size_y > 0 AND size_z > 0 AND size_x <= 1.7976931348623157e308 "
        "AND size_y <= 1.7976931348623157e308 AND size_z <= 1.7976931348623157e308 "
        "AND radius IS NULL AND length IS NULL AND mesh_blob IS NOT NULL "
        "AND length(mesh_blob) BETWEEN 12 AND 67108864 AND cad_source_name IS NOT NULL "
        "AND %1 AND cad_source_sha256 IS NOT NULL AND length(cad_source_sha256) = 64 "
        "AND cad_source_sha256 NOT GLOB '*[^0-9a-fA-F]*'))) STRICT");
    return sql.arg(nonBlank(QStringLiteral("cad_source_name")));
}

QVariant nullable(const std::optional<double> &value)
{
    return value ? QVariant(*value) : QVariant(QMetaType::fromType<double>());
}

std::optional<double> optionalValue(const QVariant &value)
{
    return value.isNull() ? std::nullopt : std::optional<double>(value.toDouble());
}

QList<Part> sampleParts()
{
    return {
        {QStringLiteral("EQ-001"), QStringLiteral("P-001"), QStringLiteral("底座"),
         QStringLiteral("Q235"), QStringLiteral("box"), 0, 0, 20,
         600, 300, 40, std::nullopt, std::nullopt},
        {QStringLiteral("EQ-001"), QStringLiteral("P-002"), QStringLiteral("驱动电机"),
         QStringLiteral("铝合金"), QStringLiteral("box"), -150, 0, 120,
         200, 160, 160, std::nullopt, std::nullopt},
        {QStringLiteral("EQ-001"), QStringLiteral("P-003"), QStringLiteral("传动轴"),
         QStringLiteral("45钢"), QStringLiteral("cylinder"), 120, 0, 120,
         std::nullopt, std::nullopt, std::nullopt, 20, 400}
    };
}
}

EquipmentRepository::EquipmentRepository() = default;

// 连接名注册与释放由SqliteConnection统一负责。
EquipmentRepository::~EquipmentRepository() = default;

bool EquipmentRepository::fail(const QString &message)
{
    error_ = message;
    return false;
}

bool EquipmentRepository::queryFailure(QSqlQuery &query, const QString &operation)
{
    return fail(operation + QStringLiteral("：") + query.lastError().text());
}

bool EquipmentRepository::execute(const QString &sql, const QString &operation)
{
    QSqlQuery query(database());
    if (query.exec(sql))
        return true;
    return queryFailure(query, operation);
}

bool EquipmentRepository::openDatabase(const QString &filePath)
{
    error_.clear();
    initialized_ = false;
    // 连接关闭/目录创建/打开由SqliteConnection共用实现；失败时补文件定位信息。
    if (!connection_.open(filePath, error_, QStringLiteral("打开数据库 ")
            + QFileInfo(filePath).absoluteFilePath() + QStringLiteral(" 失败：")))
        return false;
    bool enabled = execute(QStringLiteral("PRAGMA foreign_keys = ON"),
                           QStringLiteral("启用外键失败"));
    if (enabled) {
        QSqlQuery query(database());
        enabled = query.exec(QStringLiteral("PRAGMA foreign_keys")) && query.next()
            && query.value(0).toInt() == 1;
        if (!enabled)
            error_ = QStringLiteral("外键约束未启用：%1").arg(query.lastError().text());
    }
    if (!enabled)
        connection_.close();
    return enabled;
}

bool EquipmentRepository::rollback()
{
    // 保留导致回滚的原始错误；回滚也失败时，连接不再允许继续使用。
    const QString original = error_;
    if (execute(QStringLiteral("ROLLBACK"), QStringLiteral("回滚事务失败"))) {
        error_ = original;
        return false;
    }
    error_ = original + QStringLiteral("；") + error_;
    initialized_ = false;
    connection_.close();
    return false;
}

bool EquipmentRepository::checkExistingSchema()
{
    QSqlQuery query(database());
    if (!query.exec(QStringLiteral(
            "SELECT name, type, sql FROM sqlite_schema WHERE name IN ('equipment','part')"))) {
        return queryFailure(query, QStringLiteral("读取表结构失败"));
    }
    QMap<QString, QString> schema;
    while (query.next()) {
        if (query.value(1).toString() != QStringLiteral("table")) {
            return fail(QStringLiteral("数据库结构异常：%1 不是表，保留原文件。")
                .arg(query.value(0).toString()));
        }
        schema.insert(query.value(0).toString(), query.value(2).toString());
    }
    if (query.lastError().isValid()) {
        return queryFailure(query, QStringLiteral("读取表结构失败"));
    }
    query.finish();
    if (schema.isEmpty()) {
        if (!execute(equipmentSchema(), QStringLiteral("创建 equipment 表失败"))
            || !execute(partSchema(), QStringLiteral("创建 part 表失败")))
            return false;
        return insertEquipmentWithParts(
            {QStringLiteral("EQ-001"), QStringLiteral("输送机构")}, sampleParts());
    }
    // 仅允许精确已知旧定义升级；SQLite重命名会给表名加引号，兼容其确定的DDL结果。
    QString partDefinition = schema.value(QStringLiteral("part"));
    partDefinition.replace(QStringLiteral("CREATE TABLE \"part\""), QStringLiteral("CREATE TABLE part"));
    if (schema.size() != 2
        || schema.value(QStringLiteral("equipment")) != equipmentSchema()
        || (partDefinition != partSchema() && partDefinition != legacyPartSchema())) {
        return fail(QStringLiteral("数据库结构异常：equipment/part 定义与 F01-1 不一致，保留原文件。 "));
    }
    if (partDefinition == legacyPartSchema() && !migratePartSchema())
        return false;
    if (!query.exec(QStringLiteral("PRAGMA quick_check"))) {
        return queryFailure(query, QStringLiteral("检查数据库完整性失败"));
    }
    while (query.next()) {
        if (query.value(0).toString() != QStringLiteral("ok")) {
            return fail(QStringLiteral("数据库完整性异常：%1").arg(query.value(0).toString()));
        }
    }
    if (query.lastError().isValid()) {
        return queryFailure(query, QStringLiteral("检查数据库完整性失败"));
    }
    if (!query.exec(QStringLiteral("PRAGMA foreign_key_check"))) {
        return queryFailure(query, QStringLiteral("检查已有外键失败"));
    }
    if (query.next()) {
        return fail(QStringLiteral("已有资料外键异常：表 %1 存在无所属设备的记录。")
            .arg(query.value(0).toString()));
    }
    if (query.lastError().isValid()) {
        return queryFailure(query, QStringLiteral("检查已有外键失败"));
    }
    return true;
}

bool EquipmentRepository::initialize()
{
    error_.clear();
    initialized_ = false;
    if (!connection_.isOpen())
        return fail(QStringLiteral("数据库未打开，无法初始化。"));
    // SQLite要求在事务外切换外键；迁移后无论成功或失败都恢复并核实。
    if (!setForeignKeys(false))
        return false;
    if (!execute(QStringLiteral("BEGIN IMMEDIATE"), QStringLiteral("开始初始化事务失败")))
        return setForeignKeys(true) && false;
    bool success = checkExistingSchema();
    if (success)
        success = execute(QStringLiteral("COMMIT"), QStringLiteral("提交初始化事务失败"));
    if (!success)
        rollback();
    const QString operationError = error_;
    if (connection_.isOpen() && !setForeignKeys(true)) {
        error_ = operationError + QStringLiteral("；") + error_;
        connection_.close();
        return false;
    }
    error_ = operationError;
    initialized_ = success;
    return success;
}

bool EquipmentRepository::setForeignKeys(bool enabled)
{
    if (!execute(enabled ? QStringLiteral("PRAGMA foreign_keys=ON")
                         : QStringLiteral("PRAGMA foreign_keys=OFF"), QStringLiteral("切换外键失败")))
        return false;
    QSqlQuery query(database());
    if (!query.exec(QStringLiteral("PRAGMA foreign_keys")) || !query.next()
        || query.value(0).toInt() != int(enabled)) {
        return fail(QStringLiteral("外键模式未按要求切换。"));
    }
    return true;
}

bool EquipmentRepository::migratePartSchema()
{
    QSqlQuery query(database());
    const QString temporaryName = QStringLiteral("__ev_part_v2");
    if (!query.exec(QStringLiteral("SELECT name FROM sqlite_schema WHERE name='__ev_part_v2' "
                                   "UNION ALL SELECT name FROM sqlite_temp_schema WHERE name='__ev_part_v2'"))
        || query.next()) {
        return fail(QStringLiteral("迁移临时表名冲突或结构读取失败，保留原资料。"));
    }
    QStringList auxiliarySql;
    if (!query.exec(QStringLiteral("SELECT sql FROM sqlite_schema WHERE tbl_name='part' "
                                   "AND type IN ('index','trigger') AND sql IS NOT NULL ORDER BY type,name"))) {
        return queryFailure(query, QStringLiteral("读取旧索引/触发器失败"));
    }
    while (query.next()) auxiliarySql.append(query.value(0).toString());
    if (query.lastError().isValid()) {
        return fail(QStringLiteral("读取旧索引/触发器失败。"));
    }
    query.finish();
    auto countRows = [&query](const QString &table, qint64 &count) {
        if (!query.exec(QStringLiteral("SELECT count(*) FROM %1").arg(table)) || !query.next()) return false;
        count = query.value(0).toLongLong(); query.finish(); return true;
    };
    qint64 originalCount = 0, migratedCount = 0;
    if (!countRows(QStringLiteral("part"), originalCount)) {
        return fail(QStringLiteral("读取旧零件行数失败。"));
    }
    QString ddl = partSchema();
    ddl.replace("CREATE TABLE part", "CREATE TABLE " + temporaryName);
    const QString fields = QStringLiteral("rowid,equipment_id,id,name,material,shape,position_x,"
        "position_y,position_z,size_x,size_y,size_z,radius,length");
    // 已知canonical表必有rowid，显式复制以保护外部索引/触发器所依赖的身份。
    if (!execute(ddl, QStringLiteral("创建升级零件表失败"))
        || !execute(QStringLiteral("INSERT INTO %1(%2) SELECT %2 FROM part").arg(temporaryName, fields),
                    QStringLiteral("复制旧零件失败"))
        || !countRows(temporaryName, migratedCount) || migratedCount != originalCount) {
        if (error_.isEmpty()) error_ = QStringLiteral("升级零件行数不一致。");
        return false;
    }
    if (!execute(QStringLiteral("DROP TABLE part"), QStringLiteral("替换旧零件表失败"))
        || !execute(QStringLiteral("ALTER TABLE %1 RENAME TO part").arg(temporaryName),
                    QStringLiteral("命名升级零件表失败"))) return false;
    for (const auto &sql : auxiliarySql)
        if (!execute(sql, QStringLiteral("恢复索引/触发器失败"))) return false;
    return true;
}

bool EquipmentRepository::requireReady()
{
    error_.clear();
    if (connection_.isOpen() && initialized_)
        return true;
    return fail(QStringLiteral("数据库未打开或未完成初始化。"));
}

bool EquipmentRepository::validate(const Equipment &equipment, const QList<Part> &parts)
{
    if (equipment.id.trimmed().isEmpty() || equipment.name.trimmed().isEmpty()) {
        return fail(QStringLiteral("设备 ID 和名称不能为空白。"));
    }
    QSet<QString> ids;
    quint64 vertices = 0, triangles = 0;
    qsizetype meshParts = 0;
    if (parts.size() > 10000) {
        return fail(QStringLiteral("设备零件数超过10000。"));
    }
    for (const Part &part : parts) {
        if (part.equipmentId != equipment.id || part.id.trimmed().isEmpty()
            || part.name.trimmed().isEmpty() || part.material.trimmed().isEmpty()
            || ids.contains(part.id)) {
            return fail(QStringLiteral("零件 %1 的所属设备、必填文本或唯一 ID 无效。").arg(part.id));
        }
        ids.insert(part.id);
        // 形状、尺寸与字段互斥规则与三维场景共用同一实现，避免能存不能画。
        const bool meshSourceValid = part.shape != QStringLiteral("mesh")
            ? part.cadSourceName.isEmpty() && part.cadSourceSha256.isEmpty()
            : !part.cadSourceName.trimmed().isEmpty() && part.cadSourceSha256.size() == 64
                && QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$"))
                       .match(part.cadSourceSha256).hasMatch();
        const bool validShape = PartValidation::hasPositiveShapeDimensions(part)
            && PartValidation::usesOnlyShapeFields(part) && meshSourceValid;
        if (!validShape || !PartValidation::hasFinitePosition(part)) {
            return fail(QStringLiteral("零件 %1 的形状、尺寸或位置无效。").arg(part.id));
        }
        if (part.mesh) {
            ++meshParts;
            vertices += quint64(part.mesh->vertices.size());
            triangles += quint64(part.mesh->triangles.size());
            QString meshError;
            if (meshParts > 1000 || vertices > MeshGeometryCodec::maxVertices
                || triangles > MeshGeometryCodec::maxTriangles
                || !PartValidation::validateMeshPart(*part.mesh, {*part.sizeX, *part.sizeY, *part.sizeZ},
                    {part.positionX, part.positionY, part.positionZ}, meshError)) {
                error_ = QStringLiteral("零件 %1 的CAD网格无效或超过合计限制：%2").arg(part.id, meshError);
                return false;
            }
        }
    }
    return true;
}

bool EquipmentRepository::insertEquipmentWithParts(const Equipment &equipment, const QList<Part> &parts)
{
    if (!validate(equipment, parts))
        return false;
    QSqlQuery query(database());
    if (!query.prepare(QStringLiteral("INSERT INTO equipment(id,name) VALUES (?,?)"))) {
        return queryFailure(query, QStringLiteral("准备设备写入失败"));
    }
    query.addBindValue(equipment.id);
    query.addBindValue(equipment.name);
    if (!query.exec()) {
        return queryFailure(query, QStringLiteral("写入设备 %1 失败").arg(equipment.id));
    }
    if (!query.prepare(QStringLiteral(
            "INSERT INTO part(equipment_id,id,name,material,shape,position_x,position_y,"
            "position_z,size_x,size_y,size_z,radius,length,mesh_blob,cad_source_name,cad_source_sha256) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"))) {
        return queryFailure(query, QStringLiteral("准备零件写入失败"));
    }
    for (const Part &part : parts) {
        query.bindValue(0, part.equipmentId);
        query.bindValue(1, part.id);
        query.bindValue(2, part.name);
        query.bindValue(3, part.material);
        query.bindValue(4, part.shape);
        query.bindValue(5, part.positionX);
        query.bindValue(6, part.positionY);
        query.bindValue(7, part.positionZ);
        query.bindValue(8, nullable(part.sizeX));
        query.bindValue(9, nullable(part.sizeY));
        query.bindValue(10, nullable(part.sizeZ));
        query.bindValue(11, nullable(part.radius));
        query.bindValue(12, nullable(part.length));
        QByteArray blob;
        if (part.mesh && !MeshGeometryCodec::encode(*part.mesh, blob, error_))
            return false;
        query.bindValue(13, part.mesh ? QVariant(blob) : QVariant(QMetaType::fromType<QByteArray>()));
        query.bindValue(14, part.mesh ? QVariant(part.cadSourceName) : QVariant(QMetaType::fromType<QString>()));
        query.bindValue(15, part.mesh ? QVariant(part.cadSourceSha256) : QVariant(QMetaType::fromType<QString>()));
        if (!query.exec()) {
            return queryFailure(query,
                QStringLiteral("写入零件 %1/%2 失败").arg(part.equipmentId, part.id));
        }
    }
    return true;
}

bool EquipmentRepository::saveEquipmentWithParts(const Equipment &equipment, const QList<Part> &parts)
{
    if (!requireReady() || !validate(equipment, parts))
        return false;
    if (!execute(QStringLiteral("BEGIN IMMEDIATE"), QStringLiteral("开始保存事务失败")))
        return false;
    if (!insertEquipmentWithParts(equipment, parts))
        return rollback();
    if (!execute(QStringLiteral("COMMIT"), QStringLiteral("提交设备资料失败")))
        return rollback();
    return true;
}

bool EquipmentRepository::loadEquipment(QList<Equipment> &equipment)
{
    equipment.clear();
    if (!requireReady())
        return false;
    QSqlQuery query(database());
    if (!query.exec(QStringLiteral("SELECT id,name FROM equipment ORDER BY id"))) {
        return queryFailure(query, QStringLiteral("查询设备失败"));
    }
    QList<Equipment> result;
    while (query.next()) {
        const Equipment item{query.value(0).toString(), query.value(1).toString()};
        if (!validate(item, {}))
            return false;
        result.append(item);
    }
    if (query.lastError().isValid()) {
        return queryFailure(query, QStringLiteral("读取设备失败"));
    }
    equipment = result;
    return true;
}

bool EquipmentRepository::loadParts(const QString &equipmentId, QList<Part> &parts)
{
    parts.clear();
    if (!requireReady())
        return false;
    if (equipmentId.trimmed().isEmpty()) {
        return fail(QStringLiteral("查询零件的设备 ID 不能为空白。"));
    }
    QSqlQuery query(database());
    if (!query.prepare(QStringLiteral(
            "SELECT equipment_id,id,name,material,shape,position_x,position_y,position_z,"
            "size_x,size_y,size_z,radius,length,length(mesh_blob),substr(mesh_blob,1,12),"
            "cad_source_name,cad_source_sha256 "
            "FROM part WHERE equipment_id = ? ORDER BY id"))) {
        return queryFailure(query, QStringLiteral("准备零件查询失败"));
    }
    query.addBindValue(equipmentId);
    if (!query.exec()) {
        return queryFailure(query, QStringLiteral("查询设备 %1 的零件失败").arg(equipmentId));
    }
    QList<Part> result;
    quint64 vertexCount = 0, triangleCount = 0;
    qsizetype meshCount = 0;
    QSqlQuery blobQuery(database());
    if (!blobQuery.prepare(QStringLiteral("SELECT mesh_blob FROM part WHERE equipment_id=? AND id=?"))) {
        return queryFailure(blobQuery, QStringLiteral("准备网格读取失败"));
    }
    while (query.next()) {
        if (result.size() >= 10000) {
            return fail(QStringLiteral("读取设备零件数超过10000。"));
        }
        result.append({query.value(0).toString(), query.value(1).toString(),
                       query.value(2).toString(), query.value(3).toString(), query.value(4).toString(),
                       query.value(5).toDouble(), query.value(6).toDouble(), query.value(7).toDouble(),
                       optionalValue(query.value(8)), optionalValue(query.value(9)),
                       optionalValue(query.value(10)), optionalValue(query.value(11)),
                       optionalValue(query.value(12))});
        Part &part = result.last();
        if (part.shape == QStringLiteral("mesh")) {
            if (++meshCount > 1000) {
                return fail(QStringLiteral("读取CAD零件数超过1000。"));
            }
            // 主查询仅取长度和12字节头，先核合法计数与合计预算，再读取完整BLOB/分配网格。
            const qint64 blobLength = query.value(13).toLongLong();
            const QByteArray header = query.value(14).toByteArray();
            if (header.size() != 12 || header.first(4) != "EVM1" || blobLength < 12
                || quint64(blobLength) > MeshGeometryCodec::maxBytes) {
                return fail(QStringLiteral("零件 %1/%2 的网格BLOB头或长度无效。").arg(equipmentId, part.id));
            }
            const auto *data = reinterpret_cast<const uchar *>(header.constData());
            const quint32 vertices = qFromLittleEndian<quint32>(data + 4);
            const quint32 triangles = qFromLittleEndian<quint32>(data + 8);
            const quint64 expected = 12ULL + 24ULL * vertices + 12ULL * triangles;
            if (!vertices || !triangles || vertices > MeshGeometryCodec::maxVertices
                || triangles > MeshGeometryCodec::maxTriangles || expected != quint64(blobLength)
                || expected > MeshGeometryCodec::maxBytes) {
                return fail(QStringLiteral("零件 %1/%2 的网格BLOB计数或期望长度无效。").arg(equipmentId, part.id));
            }
            if (vertexCount + vertices > MeshGeometryCodec::maxVertices
                || triangleCount + triangles > MeshGeometryCodec::maxTriangles) {
                return fail(QStringLiteral("读取CAD合计顶点/三角形超过限制（零件 %1）。").arg(part.id));
            }
            vertexCount += vertices;
            triangleCount += triangles;
            blobQuery.bindValue(0, equipmentId); blobQuery.bindValue(1, part.id);
            if (!blobQuery.exec() || !blobQuery.next()) {
                return queryFailure(blobQuery,
                    QStringLiteral("零件 %1/%2 的网格读取失败").arg(equipmentId, part.id));
            }
            const QByteArray blob = blobQuery.value(0).toByteArray();
            blobQuery.finish();
            if (blob.size() != blobLength || blob.first(12) != header) {
                return fail(QStringLiteral("零件 %1/%2 的网格读取与已检查头部不一致。").arg(equipmentId, part.id));
            }
            auto mesh = std::make_shared<MeshGeometry>();
            if (!MeshGeometryCodec::decode(blob, *mesh, error_)) {
                error_ = QStringLiteral("零件 %1/%2 的网格读取失败：%3").arg(equipmentId, part.id, error_);
                return false;
            }
            part.mesh = std::move(mesh);
            part.cadSourceName = query.value(15).toString();
            part.cadSourceSha256 = query.value(16).toString();
        } else if (!query.value(13).isNull() || !query.value(14).isNull()
                   || !query.value(15).isNull() || !query.value(16).isNull()) {
            return fail(QStringLiteral("基本几何零件 %1 含意外CAD数据。").arg(part.id));
        }
    }
    if (query.lastError().isValid()) {
        return queryFailure(query, QStringLiteral("读取设备 %1 的零件失败").arg(equipmentId));
    }
    if (!validate({equipmentId, QStringLiteral("查询")}, result))
        return false;
    parts = result;
    return true;
}
