#pragma once

#include "equipmentrepository.h"

#include <QSet>
#include <QList>
#include <QPointF>
#include <QWidget>
#include <QColor>
#include <QVector3D>
#include <array>
#include <functional>
#include <memory>

/** 三维零件视图；只接收数据与状态，不访问数据库。 */
class SceneView : public QWidget {
public:
    enum class ViewMode { Isometric, Engineering, Front, Top, Right, ThreeViews };
    enum class AxisPlacement { Corner, Model, Both, Hidden };
    enum class ExplodeAxis { Auto, X, Y, Z };
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
    /** 原生正交工程视图；三视图使用同一像素/毫米比例。 */
    void setViewMode(ViewMode mode);
    /** XYZ显示在角落、模型旁、同时或隐藏。 */
    void setAxisPlacement(AxisPlacement placement);
    /** 尺寸默认关闭；开启后在画布边缘列出真实包络尺寸。 */
    void setDimensionsVisible(bool visible);
    /** 开关原生参考网格。 */
    void setGridVisible(bool visible);
    /** 自动取原中心跨度最大轴；无跨度时取源包络长轴。 */
    void setExplodeAxis(ExplodeAxis axis);
    /** 0..1有序外向轴伸展；非法值拒绝，不修改源坐标。 */
    bool setExplosionFraction(double fraction);
    /** 当前选中件额外外向拉远；无选择/非法值拒绝。 */
    bool setPullFraction(double fraction);
    /** 同轴中心重合时选用正/负备用方向，其他件仍向外。 */
    bool setPullDirection(int direction);
    /** 所有显示偏移复原，保留颜色、选择与隐藏状态。 */
    void restoreDisplayOffsets();
    /** 聚焦当前可见几何，保留视图模式与旋转角。 */
    void focusVisible();
    /** 聚焦可见选中件；无选择或隐藏时返回false。 */
    bool focusSelected();
    /** 指定零件自定义颜色；非法ID/颜色拒绝。 */
    bool setPartColor(const QString &id, const QColor &color);
    /** 恢复该零件稳定默认色。 */
    bool resetPartColor(const QString &id);
    /** 当前零件颜色；无效ID返回无效QColor。 */
    QColor partColor(const QString &id) const;
    /** 返回该件缓存的边界/至少1度折角边数，剔除共面内部对角线；无效ID为零。 */
    int partEdgeCount(const QString &id) const;
    /** 显示偏移毫米副本，long double保护极端源范围；无效ID为零。 */
    std::array<long double, 3> displayOffset(const QString &id) const;
    /** 返回当前彩色、工程或正交视图模式。 */
    ViewMode viewMode() const;
    /** 返回当前XYZ指示器显示位置选项。 */
    AxisPlacement axisPlacement() const;
    /** 返回实际展开轴X/Y/Z，自动选项已解析。 */
    ExplodeAxis resolvedExplodeAxis() const;
    /** 方向轴的画布几何：原点与三根世界轴端点（画布逻辑像素，端点已含真实投影压缩）。 */
    struct AxisGizmo {
        QPointF origin;
        std::array<QPointF, 3> tips{};
        bool valid = false;
    };
    /** 返回角落(true)或模型旁(false)方向轴的当前几何副本；无可见零件时 valid=false。
     *  panel 选择三视图分栏（0 正视、1 俯视、2 右视），其它视图模式忽略该参数；
     *  供验收与诊断读取，不改变绘制状态。 */
    AxisGizmo axisGizmo(bool corner, int panel = 0) const;
    /** 返回XYZ包络读数开关；参考比例尺独立保留。 */
    bool dimensionsVisible() const;
    /** 返回毫米网格叠层开关；步长按源尺度取1/2/5并在角落标明。 */
    bool gridVisible() const;
    /** 返回依据源尺度选出的网格毫米步长；无有效场景时为零。 */
    long double gridStepMillimeters() const;
    /** 返回每个视图实际参考网格线数（受限地面网格，最多58）；关闭或无有效几何时为零。 */
    int gridLineCount() const;
    /** 返回参考网格方阵的半宽（毫米，X/Y相同）；关闭、三视图或地面与视线平行时为零。
     *  该值随可见几何收敛，不随缩放无限延伸，用于核对网格不会重新变成无限平面。 */
    std::array<long double, 2> gridExtentMillimeters() const;
    /** 返回当前0..1整体显示伸展比例。 */
    double explosionFraction() const;
    /** 返回当前0..1选中件额外拉远比例。 */
    double pullFraction() const;
    /** 返回当前相机观察点（显示坐标系）；右键平移只改这里，零件源位置与尺寸不变。 */
    QVector3D cameraTarget() const;
    /** 返回当前渲染方式及回退原因。 */
    QString rendererDescription() const;
    /** 当前画面的毫米标注；空axis为参考比例尺，XYZ为选中/唯一可见件原包络或整体显示包络。
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
    /** 单击零件回传ID，空白单击无动作；空白双击回传空ID。
     *  鼠标约定与网页版一致：左键拖动旋转，右键拖动平移（正交视图也可用），
     *  中键拖动缩放，滚轮缩放；右键与中键拖动都不改变选择。 */
    std::function<void(const QString &)> onPartPicked;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
