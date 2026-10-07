#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <optional>

struct Equipment {
    QString id;
    QString name;
};

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
    QString databasePath() const { return database_.databaseName(); }
    QString lastError() const { return error_; }

private:
    bool requireReady();
    bool execute(const QString &sql, const QString &operation);
    bool rollback();
    bool insertEquipmentWithParts(const Equipment &equipment, const QList<Part> &parts);
    bool validate(const Equipment &equipment, const QList<Part> &parts);
    bool checkExistingSchema();
    QString connectionName_;
    QSqlDatabase database_;
    QString error_;
    bool initialized_ = false;
};
