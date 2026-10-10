#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <optional>
#include <memory>
#include "meshgeometry.h"
#include "sqliteconnection.h"

class QSqlQuery;

struct Equipment {
    QString id;
    QString name;
};

/** 基础几何或CAD网格零件；mesh采用不可变共享所有权，尺寸为毫米外包络。 */
struct Part {
    QString equipmentId;
    QString id;
    QString name;
    QString material;
    QString shape;
    double positionX = 0;
    double positionY = 0;
    double positionZ = 0;
    std::optional<double> sizeX;
    std::optional<double> sizeY;
    std::optional<double> sizeZ;
    std::optional<double> radius;
    std::optional<double> length;
    std::shared_ptr<const MeshGeometry> mesh = {};
    QString cadSourceName = {};
    QString cadSourceSha256 = {};
};

class EquipmentRepository {
public:
    EquipmentRepository();
    ~EquipmentRepository();
    EquipmentRepository(const EquipmentRepository &) = delete;
    EquipmentRepository &operator=(const EquipmentRepository &) = delete;

    bool openDatabase(const QString &filePath);
    bool initialize();
    bool loadEquipment(QList<Equipment> &equipment);
    bool loadParts(const QString &equipmentId, QList<Part> &parts);
    // 底层原子保存操作；本步不提供导入或编辑界面。
    bool saveEquipmentWithParts(const Equipment &equipment, const QList<Part> &parts);
    QString databasePath() const { return connection_.databasePath(); }
    QString lastError() const { return error_; }

private:
    bool requireReady();
    bool execute(const QString &sql, const QString &operation);
    bool rollback();
    bool insertEquipmentWithParts(const Equipment &equipment, const QList<Part> &parts);
    bool validate(const Equipment &equipment, const QList<Part> &parts);
    bool checkExistingSchema();
    bool migratePartSchema();
    bool setForeignKeys(bool enabled);
    /** 记录失败原因并返回false，避免每处重复赋值再return false。 */
    bool fail(const QString &message);
    /** 查询失败统一记录为“操作：SQLite错误”，保持可定位的操作名。 */
    bool queryFailure(QSqlQuery &query, const QString &operation);
    QSqlDatabase &database() { return connection_.database(); }
    SqliteConnection connection_;
    QString error_;
    bool initialized_ = false;
};
