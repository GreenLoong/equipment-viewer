#include "equipmentcontroller.h"

EquipmentController::EquipmentController(EquipmentRepository &repository)
    : repository_(repository)
{
}

bool EquipmentController::loadEquipment(const QString &equipmentId)
{
    error_.clear();
    QList<Equipment> available;
    if (!repository_.loadEquipment(available)) {
        error_ = QStringLiteral("加载设备失败：%1").arg(repository_.lastError());
        return false;
    }
    std::optional<Equipment> equipment;
    for (const Equipment &item : available) {
        if (item.id == equipmentId) {
            equipment = item;
            break;
        }
    }
    if (!equipment) {
        error_ = QStringLiteral("设备 %1 不存在。").arg(equipmentId);
        return false;
    }
    QList<Part> parts;
    if (!repository_.loadParts(equipmentId, parts)) {
        error_ = QStringLiteral("加载设备 %1 的零件失败：%2")
            .arg(equipmentId, repository_.lastError());
        return false;
    }
    // 查询全部成功后才替换状态，失败时保留原设备、零件和选择。
    equipment_ = equipment;
    parts_ = parts;
    selectedPartId_.clear();
    return true;
}

const Part *EquipmentController::findPart(const QString &partId) const
{
    for (const Part &part : parts_) {
        if (part.id == partId)
            return &part;
    }
    return nullptr;
}

bool EquipmentController::selectPart(const QString &partId)
{
    error_.clear();
    // 空 ID 表示清单当前没有选择，保持窗口与控制器的状态一致。
    if (partId.isEmpty()) {
        selectedPartId_.clear();
        return true;
    }
    if (!findPart(partId)) {
        error_ = QStringLiteral("零件 ID %1 不在当前设备中。").arg(partId);
        return false;
    }
    selectedPartId_ = partId;
    return true;
}

const Part *EquipmentController::selectedPart() const
{
    return selectedPartId_.isEmpty() ? nullptr : findPart(selectedPartId_);
}
