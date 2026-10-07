#include "equipmentrepository.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
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

QString partSchema()
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

QVariant nullable(const std::optional<double> &value)
{
    return value ? QVariant(*value) : QVariant(QMetaType::fromType<double>());
}

std::optional<double> optionalValue(const QVariant &value)
{
    return value.isNull() ? std::nullopt : std::optional<double>(value.toDouble());
}

bool positive(const std::optional<double> &value)
{
    return value && std::isfinite(*value) && *value > 0;
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

EquipmentRepository::EquipmentRepository()
    : connectionName_(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      database_(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_))
{
    database_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=1000"));
}

EquipmentRepository::~EquipmentRepository()
{
    database_.close();
    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName_);
}

bool EquipmentRepository::execute(const QString &sql, const QString &operation)
{
    QSqlQuery query(database_);
    if (query.exec(sql))
        return true;
    error_ = QStringLiteral("%1：%2").arg(operation, query.lastError().text());
    return false;
}

bool EquipmentRepository::openDatabase(const QString &filePath)
{
    error_.clear();
    initialized_ = false;
    database_.close();
    database_.setDatabaseName(QString());
    if (filePath.trimmed().isEmpty()) {
        error_ = QStringLiteral("数据库路径不能为空。");
        return false;
    }
    const QFileInfo file(filePath);
    database_.setDatabaseName(file.absoluteFilePath());
    if (!QDir().mkpath(file.absolutePath())) {
        error_ = QStringLiteral("无法创建数据库目录：%1").arg(file.absolutePath());
        return false;
    }
    if (!database_.open()) {
        error_ = QStringLiteral("打开数据库 %1 失败：%2")
            .arg(databasePath(), database_.lastError().text());
        return false;
    }
    bool enabled = execute(QStringLiteral("PRAGMA foreign_keys = ON"),
                           QStringLiteral("启用外键失败"));
    if (enabled) {
        QSqlQuery query(database_);
        enabled = query.exec(QStringLiteral("PRAGMA foreign_keys")) && query.next()
            && query.value(0).toInt() == 1;
        if (!enabled)
            error_ = QStringLiteral("外键约束未启用：%1").arg(query.lastError().text());
    }
    if (!enabled)
        database_.close();
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
    database_.close();
    return false;
}

bool EquipmentRepository::checkExistingSchema()
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT name, type, sql FROM sqlite_schema WHERE name IN ('equipment','part')"))) {
        error_ = QStringLiteral("读取表结构失败：%1").arg(query.lastError().text());
        return false;
    }
    QMap<QString, QString> schema;
    while (query.next()) {
        if (query.value(1).toString() != QStringLiteral("table")) {
            error_ = QStringLiteral("数据库结构异常：%1 不是表，保留原文件。")
                .arg(query.value(0).toString());
            return false;
        }
        schema.insert(query.value(0).toString(), query.value(2).toString());
    }
    if (query.lastError().isValid()) {
        error_ = QStringLiteral("读取表结构失败：%1").arg(query.lastError().text());
        return false;
    }
    query.finish();
    if (schema.isEmpty()) {
        if (!execute(equipmentSchema(), QStringLiteral("创建 equipment 表失败"))
            || !execute(partSchema(), QStringLiteral("创建 part 表失败")))
            return false;
        return insertEquipmentWithParts(
            {QStringLiteral("EQ-001"), QStringLiteral("输送机构")}, sampleParts());
    }
    // 首版仅接受本版本建表定义，结构缺失或约束变化不做自动修复或补种。
    if (schema.size() != 2
        || schema.value(QStringLiteral("equipment")) != equipmentSchema()
        || schema.value(QStringLiteral("part")) != partSchema()) {
        error_ = QStringLiteral("数据库结构异常：equipment/part 定义与 F01-1 不一致，保留原文件。 ");
        return false;
    }
    if (!query.exec(QStringLiteral("PRAGMA quick_check"))) {
        error_ = QStringLiteral("检查数据库完整性失败：%1").arg(query.lastError().text());
        return false;
    }
    while (query.next()) {
        if (query.value(0).toString() != QStringLiteral("ok")) {
            error_ = QStringLiteral("数据库完整性异常：%1").arg(query.value(0).toString());
            return false;
        }
    }
    if (query.lastError().isValid()) {
        error_ = QStringLiteral("检查数据库完整性失败：%1").arg(query.lastError().text());
        return false;
    }
    if (!query.exec(QStringLiteral("PRAGMA foreign_key_check"))) {
        error_ = QStringLiteral("检查已有外键失败：%1").arg(query.lastError().text());
        return false;
    }
    if (query.next()) {
        error_ = QStringLiteral("已有资料外键异常：表 %1 存在无所属设备的记录。")
            .arg(query.value(0).toString());
        return false;
    }
    if (query.lastError().isValid()) {
        error_ = QStringLiteral("检查已有外键失败：%1").arg(query.lastError().text());
        return false;
    }
    return true;
}

bool EquipmentRepository::initialize()
{
    error_.clear();
    initialized_ = false;
    if (!database_.isOpen()) {
        error_ = QStringLiteral("数据库未打开，无法初始化。");
        return false;
    }
    // 先取得写锁，两个进程同时首次启动时也不会各自判断为未初始化。
    if (!execute(QStringLiteral("BEGIN IMMEDIATE"), QStringLiteral("开始初始化事务失败")))
        return false;
    if (!checkExistingSchema())
        return rollback();
    if (!execute(QStringLiteral("COMMIT"), QStringLiteral("提交初始化事务失败")))
        return rollback();
    initialized_ = true;
    return true;
}

bool EquipmentRepository::requireReady()
{
    error_.clear();
    if (database_.isOpen() && initialized_)
        return true;
    error_ = QStringLiteral("数据库未打开或未完成初始化。");
    return false;
}

bool EquipmentRepository::validate(const Equipment &equipment, const QList<Part> &parts)
{
    if (equipment.id.trimmed().isEmpty() || equipment.name.trimmed().isEmpty()) {
        error_ = QStringLiteral("设备 ID 和名称不能为空白。");
        return false;
    }
    QSet<QString> ids;
    for (const Part &part : parts) {
        if (part.equipmentId != equipment.id || part.id.trimmed().isEmpty()
            || part.name.trimmed().isEmpty() || part.material.trimmed().isEmpty()
            || ids.contains(part.id)) {
            error_ = QStringLiteral("零件 %1 的所属设备、必填文本或唯一 ID 无效。").arg(part.id);
            return false;
        }
        ids.insert(part.id);
        const bool validShape =
            (part.shape == QStringLiteral("box") && positive(part.sizeX)
             && positive(part.sizeY) && positive(part.sizeZ) && !part.radius && !part.length)
            || (part.shape == QStringLiteral("cylinder") && positive(part.radius)
                && positive(part.length) && !part.sizeX && !part.sizeY && !part.sizeZ);
        if (!validShape || !std::isfinite(part.positionX) || !std::isfinite(part.positionY)
            || !std::isfinite(part.positionZ)) {
            error_ = QStringLiteral("零件 %1 的形状、尺寸或位置无效。").arg(part.id);
            return false;
        }
    }
    return true;
}

bool EquipmentRepository::insertEquipmentWithParts(const Equipment &equipment, const QList<Part> &parts)
{
    if (!validate(equipment, parts))
        return false;
    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral("INSERT INTO equipment(id,name) VALUES (?,?)"))) {
        error_ = QStringLiteral("准备设备写入失败：%1").arg(query.lastError().text());
        return false;
    }
    query.addBindValue(equipment.id);
    query.addBindValue(equipment.name);
    if (!query.exec()) {
        error_ = QStringLiteral("写入设备 %1 失败：%2").arg(equipment.id, query.lastError().text());
        return false;
    }
    if (!query.prepare(QStringLiteral(
            "INSERT INTO part(equipment_id,id,name,material,shape,position_x,position_y,"
            "position_z,size_x,size_y,size_z,radius,length) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)"))) {
        error_ = QStringLiteral("准备零件写入失败：%1").arg(query.lastError().text());
        return false;
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
        if (!query.exec()) {
            error_ = QStringLiteral("写入零件 %1/%2 失败：%3")
                .arg(part.equipmentId, part.id, query.lastError().text());
            return false;
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
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT id,name FROM equipment ORDER BY id"))) {
        error_ = QStringLiteral("查询设备失败：%1").arg(query.lastError().text());
        return false;
    }
    QList<Equipment> result;
    while (query.next()) {
        const Equipment item{query.value(0).toString(), query.value(1).toString()};
        if (!validate(item, {}))
            return false;
        result.append(item);
    }
    if (query.lastError().isValid()) {
        error_ = QStringLiteral("读取设备失败：%1").arg(query.lastError().text());
        return false;
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
        error_ = QStringLiteral("查询零件的设备 ID 不能为空白。");
        return false;
    }
    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral(
            "SELECT equipment_id,id,name,material,shape,position_x,position_y,position_z,"
            "size_x,size_y,size_z,radius,length FROM part WHERE equipment_id = ? ORDER BY id"))) {
        error_ = QStringLiteral("准备零件查询失败：%1").arg(query.lastError().text());
        return false;
    }
    query.addBindValue(equipmentId);
    if (!query.exec()) {
        error_ = QStringLiteral("查询设备 %1 的零件失败：%2").arg(equipmentId, query.lastError().text());
        return false;
    }
    QList<Part> result;
    while (query.next()) {
        result.append({query.value(0).toString(), query.value(1).toString(),
                       query.value(2).toString(), query.value(3).toString(), query.value(4).toString(),
                       query.value(5).toDouble(), query.value(6).toDouble(), query.value(7).toDouble(),
                       optionalValue(query.value(8)), optionalValue(query.value(9)),
                       optionalValue(query.value(10)), optionalValue(query.value(11)),
                       optionalValue(query.value(12))});
    }
    if (query.lastError().isValid()) {
        error_ = QStringLiteral("读取设备 %1 的零件失败：%2").arg(equipmentId, query.lastError().text());
        return false;
    }
    if (!validate({equipmentId, QStringLiteral("查询")}, result))
        return false;
    parts = result;
    return true;
}
