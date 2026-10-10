#pragma once

#include "equipmentrepository.h"
#include <QByteArray>
#include <QSet>

/** 管理设备资料和查看状态，不直接操作窗口或三维控件。 */
class EquipmentController {
public:
    /** 借用Repository；调用方须保证它比控制器存活更久。 */
    explicit EquipmentController(EquipmentRepository &repository);
    /** 查询可打开设备；失败返回false，当前显示状态保持。 */
    bool availableEquipment(QList<Equipment> &equipment);
    /** 完整加载成功后替换资料并重置查看状态，失败保留旧状态。 */
    bool loadEquipment(const QString &equipmentId);
    /** 解析并原子保存新设备；重复ID拒绝，保存后尝试打开。 */
    bool importJson(const QByteArray &json);
    /** 严格校验CAD网格结果后事务保存，完整读回成功才替换查看状态。 */
    bool importCadResult(const QByteArray &result);
    /** 按内存ID选择；空ID只清选中并保隔离，隔离中选择有效新件自动切隔离。 */
    bool selectPart(const QString &partId);
    /** 设置指定零件可见性，失败不改变状态。 */
    bool setPartVisible(const QString &partId, bool visible);
    /** 隐藏当前选择，保留选择及资料；无选择时返回false。 */
    bool hideSelected();
    /** 仅显示当前选择；无选择时返回false。 */
    bool isolateSelected();
    /** 清除全部隐藏和隔离，保留当前选择。 */
    void showAll();
    /** 返回当前设备只读资料；成功重新加载后原引用可能失效。 */
    const std::optional<Equipment> &equipment() const { return equipment_; }
    /** 返回当前内存零件；不跨成功重新加载保存内部对象指针。 */
    const QList<Part> &parts() const { return parts_; }
    /** 返回当前选择的设备内零件ID，未选择时为空。 */
    QString selectedPartId() const { return selectedPartId_; }
    /** 借用内存对象，仅供同步更新；重新加载后旧指针失效。 */
    const Part *selectedPart() const;
    /** 返回当前不可见零件ID集合，只读。 */
    const QSet<QString> &hiddenPartIds() const { return hiddenPartIds_; }
    /** 是否处于显式隔离模式；选择有效零件时隔离随之切换。 */
    bool isIsolationActive() const { return isolationActive_; }
    /** 当前隔离目标；清空选择或隐藏目标不丢失目标，显示全部后为空。 */
    QString isolatedPartId() const { return isolatedPartId_; }
    /** 返回最近操作错误；成功操作清除旧错误。 */
    QString lastError() const { return error_; }

private:
    const Part *findPart(const QString &partId) const;
    /** 保存已解析设备并完整重新加载；提交成功后读取失败不谎报保存失败。 */
    bool commitImportedEquipment(const Equipment &equipment, const QList<Part> &parts, bool cadResult);
    EquipmentRepository &repository_;
    std::optional<Equipment> equipment_;
    QList<Part> parts_;
    QString selectedPartId_;
    QSet<QString> hiddenPartIds_;
    bool isolationActive_ = false;
    QString isolatedPartId_;
    QString error_;
};
