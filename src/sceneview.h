#pragma once

#include "equipmentrepository.h"

#include <QSet>
#include <QWidget>
#include <functional>
#include <memory>

/** 三维零件视图；只接收数据与状态，不访问数据库。 */
class SceneView : public QWidget {
public:
    /** 创建视图；无图形上下文的平台使用明确的软件投影回退。 */
    explicit SceneView(QWidget *parent = nullptr);
    /** 在内部场景状态释放之前销毁借用该状态的渲染控件。 */
    ~SceneView() override;
    /** 复制当前设备零件并重置相机；非法几何以范围错误提示。 */
    void setParts(const QList<Part> &parts);
    /** 更新高亮 ID；隐藏零件保持隐藏。 */
    void setSelectedPart(const QString &id);
    /** 更新隐藏集合；隐藏对象不绘制、不拾取。 */
    void setHiddenParts(const QSet<QString> &ids);
    /** 恢复初始视角，使全部设备几何位于视野内。 */
    void fitAll();
    /** 返回当前渲染方式及回退原因。 */
    QString rendererDescription() const;
    /** 短点击回传零件 ID；空白处回传空 ID，用于清除选择。 */
    std::function<void(const QString &)> onPartPicked;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
