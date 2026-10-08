#include "equipmentcontroller.h"
#include "equipmentimporter.h"
#include "cadresultparser.h"

EquipmentController::EquipmentController(EquipmentRepository &repository)
    : repository_(repository)
{
}

bool EquipmentController::availableEquipment(QList<Equipment> &equipment)
{
    error_.clear();
    if (repository_.loadEquipment(equipment))
        return true;
    error_ = QStringLiteral("读取设备列表失败：%1").arg(repository_.lastError());
    return false;
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
    // 全部查询成功后提交查看状态，避免失败时留下半个新设备。
    equipment_ = equipment;
    parts_ = parts;
    selectedPartId_.clear();
    hiddenPartIds_.clear();
    isolationActive_ = false;
    return true;
}

bool EquipmentController::importJson(const QByteArray &json)
{
    error_.clear();
    Equipment equipment;
    QList<Part> parts;
    if (!EquipmentImporter::parse(json, equipment, parts, error_))
        return false;
    if (!repository_.saveEquipmentWithParts(equipment, parts)) {
        error_ = QStringLiteral("保存导入设备 %1 失败：%2")
            .arg(equipment.id, repository_.lastError());
        return false;
    }
    if (!loadEquipment(equipment.id)) {
        // 提交成功后读取失败不能声称保存失败，重试导入会触发重复ID。
        error_ = QStringLiteral("设备 %1 已保存，但打开失败：%2。请稍后从设备列表打开。")
            .arg(equipment.id, error_);
        return false;
    }
    return true;
}

bool EquipmentController::importCadResult(const QByteArray &result)
{
    error_.clear();
    Equipment equipment;
    QList<Part> parts;
    if (!CadResultParser::parse(result, equipment, parts, error_))
        return false;
    if (!repository_.saveEquipmentWithParts(equipment, parts)) {
        error_ = QStringLiteral("保存CAD设备失败：%1").arg(repository_.lastError());
        return false;
    }
    if (!loadEquipment(equipment.id)) {
        error_ = QStringLiteral("CAD设备已保存，但完整打开失败：%1。请稍后从设备列表打开。")
            .arg(error_);
        return false;
    }
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

bool EquipmentController::setPartVisible(const QString &partId, bool visible)
{
    error_.clear();
    if (!findPart(partId)) {
        error_ = QStringLiteral("零件 %1 不在当前设备中。").arg(partId);
        return false;
    }
    if (visible)
        hiddenPartIds_.remove(partId);
    else
        hiddenPartIds_.insert(partId);
    // 显式显示多个对象后不再是隔离；隐藏隔离件可暂时为空，仍允许双击切换。
    if (visible && isolationActive_) {
        for (const Part &part : parts_) {
            if (part.id != partId && !hiddenPartIds_.contains(part.id)) {
                isolationActive_ = false;
                break;
            }
        }
    }
    return true;
}

bool EquipmentController::hideSelected()
{
    if (!selectedPart()) {
        error_ = QStringLiteral("请先选择一个零件。");
        return false;
    }
    return setPartVisible(selectedPartId_, false);
}

bool EquipmentController::isolateSelected()
{
    error_.clear();
    if (!selectedPart()) {
        error_ = QStringLiteral("请先选择一个零件。");
        return false;
    }
    // 隔离是显式查看操作，可恢复选中对象，但不改变其身份或属性。
    isolationActive_ = true;
    hiddenPartIds_.clear();
    for (const Part &part : parts_) {
        if (part.id != selectedPartId_)
            hiddenPartIds_.insert(part.id);
    }
    return true;
}

void EquipmentController::showAll()
{
    error_.clear();
    hiddenPartIds_.clear();
    isolationActive_ = false;
}
