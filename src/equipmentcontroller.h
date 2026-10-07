#pragma once

#include "equipmentrepository.h"

class EquipmentController {
public:
    explicit EquipmentController(EquipmentRepository &repository);

    bool loadEquipment(const QString &equipmentId);
    bool selectPart(const QString &partId);
    const std::optional<Equipment> &equipment() const { return equipment_; }
    const QList<Part> &parts() const { return parts_; }
    QString selectedPartId() const { return selectedPartId_; }
    // 借用内存中的对象，仅供当前更新使用；成功重新加载后旧指针失效。
    const Part *selectedPart() const;
    QString lastError() const { return error_; }

private:
    const Part *findPart(const QString &partId) const;
    EquipmentRepository &repository_;
    std::optional<Equipment> equipment_;
    QList<Part> parts_;
    QString selectedPartId_;
    QString error_;
};
