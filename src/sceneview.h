#pragma once

#include "equipmentrepository.h"

#include <QSet>
#include <QList>
#include <QPointF>
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
    /** 当前画面的毫米标注；空 axis 为中心截面比例尺，XYZ 为选中件外包络。
     *  start/end 使用画布逻辑像素；lineVisible=false 时只显示数值，避免短投影误导。
     *  数值使用 long double，避免极端圆柱外径乘二溢出 double。 */
    struct MeasurementAnnotation {
        QString axis;
        QString label;
        long double millimeters = 0;
        QPointF start;
        QPointF end;
        bool lineVisible = false;
    };
    /** 返回当前可见标注副本；空场景、范围错误或全部隐藏时为空。 */
    QList<MeasurementAnnotation> measurementAnnotations() const;
    /** 短点击回传零件 ID；空白处回传空 ID，用于清除选择。 */
    std::function<void(const QString &)> onPartPicked;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
