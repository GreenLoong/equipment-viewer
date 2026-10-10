#include "sceneview.h"
#include "numericutil.h"
#include "partvalidation.h"

#include <QApplication>
#include <QMatrix4x4>
#include <QMetaObject>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPointer>
#include <QPolygonF>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector3D>
#include <QVector4D>
#include <QWheelEvent>
#include <QHash>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <new>

namespace {
constexpr double pi = 3.14159265358979323846;
struct Triangle {
    QVector3D a, b, c;
    QString id;
    /** 三个角点的顶点光照；同一平面三角形三个角取值相同，曲面相邻三角面之间连续。 */
    std::array<float, 3> light = {1, 1, 1};
};

struct SceneEdge {
    QVector3D a, b;
    QString id;
};

struct ProjectedEdge {
    QLineF line;
    QString id;
    QRect viewport;
    float depth = 0;
};

struct ProjectedFace {
    QPolygonF polygon;
    std::array<float, 3> depths;
    QString id;
    QColor color;
    float depth = 0;
    QRect viewport;
};

struct PartBounds {
    QString id;
    bool cylinder = false;
    QVector3D low;
    QVector3D high;
    std::array<long double, 3> millimeters;
};

/** 观察方向与屏幕上方；透视、正交与三视图分栏共用同一解析结果。 */
struct CameraOrientation {
    QVector3D direction;
    QVector3D up;
};

/**
 * 解析某视图模式（三视图按分栏取正/俯/右视）的观察方向与屏幕上方。
 * 相机基向量、视图矩阵与方向轴原先各写一份球坐标换算，集中在此避免不一致。
 */
CameraOrientation cameraOrientation(SceneView::ViewMode viewMode, int panel, float yaw, float pitch)
{
    const float y = yaw * float(pi / 180), p = pitch * float(pi / 180);
    CameraOrientation result;
    // 数据 Z 向上；以球坐标构建相机，避免把数据轴旋转成 Qt 屏幕轴。
    result.direction = QVector3D(std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p));
    result.up = QVector3D(0, 0, 1);
    auto mode = viewMode;
    if (mode == SceneView::ViewMode::ThreeViews)
        mode = panel == 0 ? SceneView::ViewMode::Front
             : panel == 1 ? SceneView::ViewMode::Top : SceneView::ViewMode::Right;
    if (mode == SceneView::ViewMode::Front)
        result.direction = QVector3D(0, -1, 0);
    else if (mode == SceneView::ViewMode::Top) {
        result.direction = QVector3D(0, 0, 1);
        result.up = QVector3D(0, 1, 0);
    } else if (mode == SceneView::ViewMode::Right)
        result.direction = QVector3D(1, 0, 0);
    return result;
}

/** 裁剪空间坐标是否落在可绘制范围内；非有限值或越界深度都拒绝。 */
bool finiteNdc(const QVector3D &ndc)
{
    return std::isfinite(ndc.x()) && std::isfinite(ndc.y()) && std::isfinite(ndc.z())
        && ndc.z() >= -1 && ndc.z() <= 1;
}

/** 归一化设备坐标到画布逻辑像素；Y轴翻转与Qt窗口坐标一致。 */
QPointF canvasPoint(const QRect &viewport, const QVector3D &ndc)
{
    return QPointF(viewport.x() + (ndc.x() + 1) * viewport.width() / 2,
                   viewport.y() + (1 - ndc.y()) * viewport.height() / 2);
}

/** 取1/2/5档的整齐步长；毫米网格与参考比例尺原先各写一份同类算法。 */
long double niceStep(long double target)
{
    const long double decade = std::pow(10.0L, std::floor(std::log10(target)));
    const long double mantissa = target / decade;
    return (mantissa >= 5 ? 5 : mantissa >= 2 ? 2 : 1) * decade;
}

/** 线框模式（正交投影）只画边不画面；原生与软件回退共用同一判定。 */
bool lineFrameMode(SceneView::ViewMode viewMode)
{
    return viewMode == SceneView::ViewMode::Front || viewMode == SceneView::ViewMode::Top
        || viewMode == SceneView::ViewMode::Right || viewMode == SceneView::ViewMode::ThreeViews;
}

/** 世界轴名称；读数、图例与方向轴共用同一身份顺序。 */
const std::array<QString, 3> &axisNames()
{
    static const std::array<QString, 3> names = {QStringLiteral("X"), QStringLiteral("Y"),
                                                 QStringLiteral("Z")};
    return names;
}

/** 读数与尺寸线颜色；空axis为参考比例尺使用的中性色。 */
QColor axisAccentColor(const QString &axis)
{
    if (axis == QStringLiteral("X")) return QColor(255, 160, 155);
    if (axis == QStringLiteral("Y")) return QColor(145, 225, 165);
    if (axis == QStringLiteral("Z")) return QColor(150, 190, 255);
    return QColor(215, 225, 235);
}

/** 右下角方向轴的三色；与读数颜色分开，保持与网页版一致的高饱和配色。 */
const std::array<QColor, 3> &axisGizmoColors()
{
    static const std::array<QColor, 3> colors = {QColor(233, 118, 113), QColor(139, 208, 154),
                                                 QColor(111, 181, 239)};
    return colors;
}

/** 追加一个带颜色顶点：位置与颜色交错存放，VBO按此布局读取。 */
void appendColoredVertex(QList<float> &buffer, const QVector3D &point, const QColor &color)
{
    buffer.append(point.x());
    buffer.append(point.y());
    buffer.append(point.z());
    buffer.append(float(color.redF()));
    buffer.append(float(color.greenF()));
    buffer.append(float(color.blueF()));
}

/** 顶点法线平滑阈值（cos 50°）：只有夹角小于该值的相邻三角面才共享平均法线。 */
constexpr float smoothNormalThreshold = 0.643f;

/** 主光方向；与原逐面光照同一方向，保证亮面分布不变。 */
const QVector3D &keyLightDirection()
{
    static const QVector3D direction = QVector3D(0.30f, 0.40f, 0.86f).normalized();
    return direction;
}

/**
 * 顶点光照：半球环境光（朝上更亮）+ 单向漫反射（双面按同向处理）。
 * 全部为乘性计算，颜色通道比例不变；取值范围与原 0.55+0.45|dot| 接近，
 * 但改为逐顶点插值后曲面不再出现逐面片的台阶色块。
 */
float vertexLight(const QVector3D &normal)
{
    const float up = 0.5f + 0.5f * normal.z();
    const float diffuse = std::abs(QVector3D::dotProduct(normal, keyLightDirection()));
    return std::clamp(0.44f + 0.24f * up + 0.38f * diffuse, 0.44f, 1.08f);
}

/**
 * 网格顶点法线：按共享顶点累加相邻三角面法线并归一化。
 * 累加前按该顶点首个面法线对齐，避免相邻三角面缠绕方向不一致时法线互相抵消；
 * 调用方再用 smoothNormalThreshold 判断硬边，棱角面继续使用面法线。
 */
QList<QVector3D> meshVertexNormals(const MeshGeometry &mesh)
{
    const auto faceNormal = [&mesh](const std::array<quint32, 3> &triangle) {
        const QVector3D a(float(mesh.vertices[triangle[0]][0]), float(mesh.vertices[triangle[0]][1]),
                          float(mesh.vertices[triangle[0]][2]));
        const QVector3D b(float(mesh.vertices[triangle[1]][0]), float(mesh.vertices[triangle[1]][1]),
                          float(mesh.vertices[triangle[1]][2]));
        const QVector3D c(float(mesh.vertices[triangle[2]][0]), float(mesh.vertices[triangle[2]][1]),
                          float(mesh.vertices[triangle[2]][2]));
        return QVector3D::crossProduct(b - a, c - a).normalized();
    };
    QList<QVector3D> references(mesh.vertices.size());
    QList<QVector3D> normals(mesh.vertices.size());
    for (const auto &triangle : mesh.triangles) {
        const QVector3D normal = faceNormal(triangle);
        for (quint32 index : triangle) {
            if (references[index].isNull()) references[index] = normal;
            normals[index] += QVector3D::dotProduct(normal, references[index]) < 0 ? -normal : normal;
        }
    }
    for (auto &normal : normals)
        normal = normal.lengthSquared() > 0 ? normal.normalized() : QVector3D();
    return normals;
}

/** 一根世界轴在画面上的投影；direction 未归一化，其长度就是真实压缩后的可见长度。 */
struct ProjectedAxis {
    QPointF direction;   // x 向右、y 向下，单位与像素半径同量纲
    QColor color;
    QString name;
    float viewDepth = 0; // 相机空间 z：越大越深入屏幕，用于近轴后画
};

/**
 * 计算三根世界轴在给定相机基向量下的真实投影，按 X、Y、Z 顺序返回。
 * 与网页版一致：不做归一化，轴与视线接近平行时长度自然压到零，只留圆点和字母；
 * 归一化会把接近零的投影放大成噪声，导致方向在 0°/180° 之间跳变。
 */
QList<ProjectedAxis> projectedAxes(const QVector3D &right, const QVector3D &upScreen,
                                   const QVector3D &forward)
{
    static const std::array<QVector3D, 3> axes = {QVector3D(1, 0, 0), QVector3D(0, 1, 0),
                                                  QVector3D(0, 0, 1)};
    const auto &colors = axisGizmoColors();
    const auto &names = axisNames();
    QList<ProjectedAxis> result;
    for (int axis = 0; axis < 3; ++axis) {
        result.append({QPointF(QVector3D::dotProduct(axes[axis], right),
                               -QVector3D::dotProduct(axes[axis], upScreen)),
                       colors[axis], names[axis],
                       float(QVector3D::dotProduct(axes[axis], forward))});
    }
    return result;
}

struct SceneData {
    QList<Triangle> triangles;
    QList<SceneEdge> edges;
    QSet<QString> hidden;
    QString selected;
    QString error;
    QString renderer;
    QList<PartBounds> bounds;
    long double millimetersPerUnit = 0;
    float yaw = 35;
    float pitch = 25;
    float distance = 4;
    float minimumDistance = 2.5f;
    SceneView::ViewMode viewMode = SceneView::ViewMode::Isometric;
    SceneView::AxisPlacement axisPlacement = SceneView::AxisPlacement::Corner;
    SceneView::ExplodeAxis explodeAxis = SceneView::ExplodeAxis::Auto;
    int automaticAxis = 0;
    bool dimensionsVisible = false;
    bool gridVisible = true;
    /** 原生GL路径自己用深度缓冲画地面网格；叠加层据此避免重复绘制与穿透实体。 */
    bool gridDrawnInGl = false;
    double explosionFraction = 0;
    double pullFraction = 0;
    int pullDirection = 1;
    QVector3D target;
    QHash<QString, QVector3D> centers;
    QHash<QString, QVector3D> offsets;
    QHash<QString, QColor> colors;
    QHash<QString, QColor> defaultColors;

    int activeAxis() const
    {
        return explodeAxis == SceneView::ExplodeAxis::Auto ? automaticAxis : int(explodeAxis) - 1;
    }

    void updateOffsets()
    {
        const int axis = activeAxis();
        for (auto it = centers.cbegin(); it != centers.cend(); ++it) {
            QVector3D offset;
            const float difference = it.value()[axis]; // 归一化原整体包络中心为零。
            const float sign = difference > 0 ? 1 : difference < 0 ? -1 : float(pullDirection);
            offset[axis] = float(difference * explosionFraction * 2)
                + (it.key() == selected ? float(pullFraction * 2.4) * sign : 0);
            offsets.insert(it.key(), offset);
        }
    }

    QList<QRect> viewports(const QSize &size) const
    {
        if (viewMode != SceneView::ViewMode::ThreeViews)
            return {QRect(QPoint(), size)};
        const int column = size.width() / 3;
        return {QRect(0, 0, column, size.height()), QRect(column, 0, column, size.height()),
                QRect(column * 2, 0, column, size.height())};
    }

    QColor background() const
    {
        if (viewMode == SceneView::ViewMode::Engineering) return QColor(247, 249, 252);
        if (viewMode == SceneView::ViewMode::Isometric) return QColor(31, 40, 52);
        return QColor(13, 18, 26);
    }

    struct GridStep {
        long double millimeters = 0;
        float units = 0;
    };

    GridStep gridStep() const
    {
        if (!std::isfinite(millimetersPerUnit) || millimetersPerUnit <= 0) return {};
        // 以源毫米比例选1/2/5步长；不把固定归一化网格宣称为毫米刻度。
        const long double targetMm = millimetersPerUnit * 0.4L;
        const long double niceMm = niceStep(targetMm);
        const long double units = niceMm / millimetersPerUnit;
        if (!std::isfinite(niceMm) || niceMm <= 0 || !std::isfinite(units) || units <= 0) return {};
        return {niceMm, float(units)};
    }

    bool cachePartEdges(qsizetype firstTriangle)
    {
        struct Entry {
            QVector3D a, b, normal;
            int references = 0;
            bool sharp = false;
        };
        // 每件独立缓存同渲染坐标的边；不跨零件合并，不以近似坐标吸并真实孔或细节。
        const qsizetype budget = (triangles.size() - firstTriangle) * 3;
        std::map<std::array<float, 6>, Entry> unique;
        const float threshold = float(std::cos(pi / 180)); // 与Demo近似的1度折角阈值。
        try {
            for (qsizetype i = firstTriangle; i < triangles.size(); ++i) {
                const auto &triangle = triangles[i];
                const auto normal = QVector3D::crossProduct(triangle.b - triangle.a, triangle.c - triangle.a).normalized();
                const std::array<QVector3D, 3> vertices = {triangle.a, triangle.b, triangle.c};
                for (int side = 0; side < 3; ++side) {
                    auto a = vertices[side], b = vertices[(side + 1) % 3];
                    std::array<float, 3> left = {a.x(), a.y(), a.z()}, right = {b.x(), b.y(), b.z()};
                    if (left == right) continue;
                    if (right < left) { std::swap(left, right); std::swap(a, b); }
                    const std::array<float, 6> key = {left[0], left[1], left[2], right[0], right[1], right[2]};
                    auto found = unique.find(key);
                    if (found == unique.end()) {
                        if (qsizetype(unique.size()) >= budget) return false;
                        unique.emplace(key, Entry{a, b, normal, 1, normal.isNull()});
                    } else {
                        auto &entry = found->second;
                        ++entry.references;
                        // 与EdgesGeometry同向法向比较；反向共边不可被abs误删为共面。
                        entry.sharp = entry.sharp || normal.isNull()
                            || QVector3D::dotProduct(entry.normal, normal) <= threshold;
                    }
                }
            }
            const QString id = firstTriangle < triangles.size() ? triangles[firstTriangle].id : QString();
            for (const auto &pair : unique) {
                const auto &entry = pair.second;
                if (entry.references != 2 || entry.sharp) edges.append({entry.a, entry.b, id});
            }
            return true;
        } catch (const std::bad_alloc &) {
            return false;
        }
    }

    QList<ProjectedEdge> projectedEdges(const QSize &size) const
    {
        QList<ProjectedEdge> result;
        const auto ports = viewports(size);
        for (int panel = 0; panel < ports.size(); ++panel) {
            const auto port = ports[panel];
            const auto mvp = matrix(port.size(), panel);
            for (const auto &edge : edges) {
                if (hidden.contains(edge.id)) continue;
                const QVector3D offset = offsets.value(edge.id);
                const auto a = mvp * QVector4D(edge.a + offset, 1);
                const auto b = mvp * QVector4D(edge.b + offset, 1);
                if (a.w() <= 0 || b.w() <= 0) continue;
                const auto an = a.toVector3DAffine(), bn = b.toVector3DAffine();
                if (!finiteNdc(an) || !finiteNdc(bn)) continue;
                result.append({QLineF(canvasPoint(port, an), canvasPoint(port, bn)),
                    edge.id, port, (an.z() + bn.z()) / 2});
            }
        }
        return result;
    }

    bool hasVisibleGeometry() const
    {
        return error.isEmpty() && std::any_of(triangles.cbegin(), triangles.cend(),
            [this](const Triangle &triangle) { return !hidden.contains(triangle.id); });
    }

    QList<SceneView::MeasurementAnnotation> annotations(const QSize &size) const
    {
        QList<SceneView::MeasurementAnnotation> result;
        if (!hasVisibleGeometry() || size.width() < 240 || size.height() < 180
            || !std::isfinite(millimetersPerUnit) || millimetersPerUnit <= 0)
            return result;
        // 透视相机中心深度为 distance；垂直视线的模型中心截面具有一致比例。
        // 此关系与实际40度FOV、距离、视口和归一化复用，不能用固定像素冒充毫米。
        const auto ports = viewports(size);
        const long double pixelsPerUnit = (viewMode == SceneView::ViewMode::Isometric ? size.height()
            : std::min(ports.first().width(), ports.first().height()))
            / (2 * std::tan(20 * pi / 180) * distance);
        const long double targetPixels = std::clamp(size.width() * 0.2, 60.0, 150.0);
        const long double targetMm = targetPixels / pixelsPerUnit * millimetersPerUnit;
        const long double niceMm = niceStep(targetMm);
        const double pixels = double(niceMm / millimetersPerUnit * pixelsPerUnit);
        if (std::isfinite(pixels) && pixels > 0) {
            result.append({{}, NumericUtil::millimeterText(niceMm), niceMm,
                QPointF(16, size.height() - 62), QPointF(16 + pixels, size.height() - 62), true});
        }
        if (!dimensionsVisible) return result;
        QList<PartBounds> visibleBounds;
        QList<PartBounds> measureBounds;
        for (const auto &bound : bounds) {
            if (hidden.contains(bound.id)) continue;
            visibleBounds.append(bound);
            if (bound.id == selected) measureBounds.append(bound);
        }
        QString scope;
        if (measureBounds.isEmpty() && visibleBounds.size() == 1) {
            measureBounds = visibleBounds;
            scope = QStringLiteral("可见件 ");
        } else if (measureBounds.isEmpty() && !visibleBounds.isEmpty()) {
            // 整体显示包络与“适配全部/聚焦”使用同一份可见包围盒计算。
            QVector3D low, high;
            if (visibleDisplayBox(low, high)) {
                PartBounds aggregate = visibleBounds.first();
                aggregate.cylinder = false;
                aggregate.low = low;
                aggregate.high = high;
                for (int axis = 0; axis < 3; ++axis)
                    aggregate.millimeters[axis] =
                        (static_cast<long double>(high[axis]) - low[axis]) * millimetersPerUnit;
                measureBounds.append(aggregate);
                scope = QStringLiteral("整体显示包络 ");
            }
        }
        for (const auto &bound : measureBounds) {
            const auto &axes = axisNames();
            for (int axis = 0; axis < 3; ++axis) {
                // 原始XYZ数值集中在边缘；不随显示偏移/相机改写尺寸。
                const QString detail = bound.cylinder
                    ? (axis == 0 ? QStringLiteral("（轴向）") : QStringLiteral("（外径）")) : QString();
                result.append({axes[axis], scope + axes[axis] + detail + QStringLiteral(" = ")
                    + NumericUtil::millimeterText(bound.millimeters[axis]), bound.millimeters[axis],
                    QPointF(12, (viewMode == SceneView::ViewMode::ThreeViews ? 40 : 16) + axis * 24),
                    QPointF(12, (viewMode == SceneView::ViewMode::ThreeViews ? 40 : 16) + axis * 24), false});
            }
            break;
        }
        return result;
    }

    QMatrix4x4 matrix(const QSize &size, int panel = 0) const
    {
        QMatrix4x4 projection, view;
        const float aspect = float(std::max(1, size.width())) / std::max(1, size.height());
        const float half = distance * float(std::tan(20 * pi / 180));
        // 各三视图统一用较短边定义比例，分栏宽度相差一像素时也保持同比例。
        const float halfX = half * std::max(1.0f, aspect), halfY = half * std::max(1.0f, 1 / aspect);
        if (viewMode == SceneView::ViewMode::Isometric)
            projection.perspective(40, aspect, std::max(0.000001f, distance * 0.001f), 200.0f);
        else projection.ortho(-halfX, halfX, -halfY, halfY, 0.01f, 200.0f);
        // 相机朝向与平移、方向轴共用同一解析，避免各自重算球坐标导致不一致。
        const CameraOrientation orientation = cameraOrientation(viewMode, panel, yaw, pitch);
        view.lookAt(target + orientation.direction * (viewMode == SceneView::ViewMode::Isometric ? distance : 80),
                    target, orientation.up);
        return projection * view;
    }

    /**
     * 指定分栏的相机基向量。
     * 三视图各栏视线不同，方向轴必须用本栏的基向量，不能三栏共用首栏朝向。
     */
    void cameraBasisAt(int panel, QVector3D &right, QVector3D &upScreen, QVector3D &forward) const
    {
        const CameraOrientation orientation = cameraOrientation(viewMode, panel, yaw, pitch);
        forward = -orientation.direction;
        right = QVector3D::crossProduct(forward, orientation.up);
        if (right.lengthSquared() < 1e-12f) right = QVector3D(1, 0, 0);
        right.normalize();
        upScreen = QVector3D::crossProduct(right, forward).normalized();
    }

    /** 单栏相机基向量；平移与方向轴共用，避免各自重算球坐标导致不一致。 */
    void cameraBasis(QVector3D &right, QVector3D &upScreen, QVector3D &forward) const
    {
        cameraBasisAt(0, right, upScreen, forward);
    }

    /** 角落方向轴半径（像素）：与网页版 120 像素画布上的 39 像素半径一致。 */
    static constexpr double cornerAxisRadius = 39;
    /** 模型旁方向轴半径（像素）：对应网页版长度约 55 像素的箭头。 */
    static constexpr double modelAxisRadius = 55;

    /** 方向轴几何：原点、三轴端点（端点 = 原点 + 真实投影 × 半径）与按轴序排列的投影信息。 */
    struct GizmoGeometry {
        QPointF origin;
        std::array<QPointF, 3> tips{};
        QList<ProjectedAxis> axes;
        double radius = 0;
        bool valid = false;
    };

    /** 模型旁方向轴落点：选中件的显示中心，否则整体可见显示包络中心，再投影到画布。 */
    QPointF gizmoModelPoint(const QRect &port, int panel) const
    {
        QVector3D center;
        bool found = false;
        for (const auto &bound : bounds) {
            if (hidden.contains(bound.id) || bound.id != selected) continue;
            center = (bound.low + bound.high) / 2 + offsets.value(bound.id);
            found = true;
            break;
        }
        if (!found) {
            // 无选中件时用整体可见显示包络；两者都不可用时中心退回原点。
            QVector3D low(100, 100, 100), high(-100, -100, -100);
            if (visibleDisplayBox(low, high)) center = (low + high) / 2;
            else center = QVector3D();
        }
        const QVector4D clip = matrix(port.size(), panel) * QVector4D(center, 1);
        if (!std::isfinite(clip.w()) || clip.w() <= 0) return port.center();
        return canvasPoint(port, clip.toVector3DAffine());
    }

    /**
     * 计算方向轴几何；绘制与验收共用同一份结果，避免两边各算一套投影。
     * 角落轴固定在右下角并留出半径与字母边距；模型旁轴跟随模型中心。
     */
    GizmoGeometry axisGeometry(bool corner, const QRect &port, int panel) const
    {
        GizmoGeometry geometry;
        QVector3D right, upScreen, forward;
        cameraBasisAt(panel, right, upScreen, forward);
        geometry.axes = projectedAxes(right, upScreen, forward);
        geometry.radius = corner ? cornerAxisRadius : modelAxisRadius;
        if (corner) {
            QPointF origin(port.right() - 62, port.bottom() - 82);
            origin.setX(std::clamp(origin.x(), double(port.left() + geometry.radius + 16),
                                   double(port.right() - geometry.radius - 16)));
            origin.setY(std::clamp(origin.y(), double(port.top() + geometry.radius + 16),
                                   double(port.bottom() - geometry.radius - 16)));
            geometry.origin = origin;
        } else {
            geometry.origin = gizmoModelPoint(port, panel) + QPointF(48, 0);
        }
        // 端点直接用真实投影：轴与视线平行时不额外补长度，自然压成圆点。
        for (int axis = 0; axis < 3; ++axis)
            geometry.tips[axis] = geometry.origin + geometry.axes[axis].direction * geometry.radius;
        geometry.valid = true;
        return geometry;
    }

    /**
     * 画一组方向轴：杆长取真实投影长度（不再用固定像素长度），末端实心圆点，
     * 字母贴在端点右上方；尺寸与网页版一致（半径 39 像素、圆点 4 像素、2 像素杆、加粗字母）。
     * withArrowheads 用于模型旁方向轴，按网页版 ArrowHelper 比例改画箭头。
     */
    void drawGizmo(QPainter &painter, const GizmoGeometry &geometry, bool withArrowheads,
                   bool lightBackground) const
    {
        // 与网页版按相机空间 z 排序等价：远端先画，靠近观察者的轴最后画。
        QList<int> order = {0, 1, 2};
        std::sort(order.begin(), order.end(), [&geometry](int a, int b) {
            return geometry.axes[a].viewDepth > geometry.axes[b].viewDepth;
        });
        QFont font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        for (int index : order) {
            const ProjectedAxis &axis = geometry.axes[index];
            const QPointF tip = geometry.tips[index];
            const double length = QLineF(geometry.origin, tip).length();
            const bool drawn = length > 0.5;
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(axis.color, 2));
            if (drawn && !withArrowheads) {
                painter.drawLine(geometry.origin, tip);
            } else if (drawn) {
                const QPointF unit = (tip - geometry.origin) / length;
                const QPointF headBase = tip - unit * std::max(6.0, geometry.radius * 0.14);
                const QPointF perpendicular(-unit.y(), unit.x());
                const double headRadius = std::max(3.0, geometry.radius * 0.075);
                painter.drawLine(geometry.origin, headBase);
                painter.setPen(Qt::NoPen);
                painter.setBrush(axis.color);
                painter.drawPolygon(QPolygonF({tip, headBase + perpendicular * headRadius,
                                               headBase - perpendicular * headRadius}));
            }
            if (!withArrowheads) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(axis.color);
                painter.drawEllipse(tip, 4, 4);
            }
            // 字母与网页版同样贴在端点右上方；轴被压短时随端点移动，不再固定在原点旁。
            QRectF labelBox = painter.fontMetrics().boundingRect(axis.name);
            labelBox.moveBottomLeft(tip + QPointF(5, -5));
            painter.fillRect(labelBox.adjusted(-2, -1, 2, 1),
                lightBackground ? QColor(255, 255, 255, 205) : QColor(24, 32, 42, 205));
            painter.setPen(axis.color);
            painter.drawText(labelBox, Qt::AlignCenter, axis.name);
        }
    }

    /** 屏幕像素到世界单位的换算；透视与正交都用同一半高关系，保证拖动跟随光标。 */
    float worldPerPixel(const QSize &size) const
    {
        const float aspect = float(std::max(1, size.width())) / std::max(1, size.height());
        const float half = distance * float(std::tan(20 * pi / 180));
        return 2 * half * std::max(1.0f, 1 / aspect) / float(std::max(1, size.height()));
    }

    /** 右键拖动平移：只移动观察点；越界或非有限位移被拒绝。 */
    bool panByPixels(const QPointF &delta, const QSize &size)
    {
        if (!std::isfinite(delta.x()) || !std::isfinite(delta.y())) return false;
        QVector3D right, upScreen, forward;
        cameraBasis(right, upScreen, forward);
        const float scale = worldPerPixel(size);
        const QVector3D move = (right * float(-delta.x()) + upScreen * float(delta.y())) * scale;
        if (!std::isfinite(move.x()) || !std::isfinite(move.y()) || !std::isfinite(move.z())) return false;
        // 归一化场景在[-1,1]附近；限幅避免把模型拖出可视范围或产生极端相机坐标。
        target = QVector3D(std::clamp(target.x() + move.x(), -20.0f, 20.0f),
                           std::clamp(target.y() + move.y(), -20.0f, 20.0f),
                           std::clamp(target.z() + move.z(), -20.0f, 20.0f));
        return true;
    }

    /** 中键拖动缩放；steps>0 为拉近。 */
    void zoomBySteps(double steps)
    {
        if (!std::isfinite(steps)) return;
        distance = std::clamp(distance * float(std::exp(-std::clamp(steps, -12.0, 12.0))), minimumDistance, 60.0f);
    }

    /**
     * 可见零件的显示包围盒（含显示偏移）；only非空时只统计该件。
     * 无可见件返回false并保持输出不变，调用方可保留自己的哨兵初值。
     */
    bool visibleDisplayBox(QVector3D &low, QVector3D &high, const QString &only = {}) const
    {
        bool found = false;
        for (const auto &bound : bounds) {
            if (hidden.contains(bound.id) || (!only.isEmpty() && bound.id != only)) continue;
            const QVector3D offset = offsets.value(bound.id);
            const QVector3D partLow = bound.low + offset, partHigh = bound.high + offset;
            if (!found) { low = partLow; high = partHigh; found = true; continue; }
            for (int axis = 0; axis < 3; ++axis) {
                low[axis] = std::min(low[axis], partLow[axis]);
                high[axis] = std::max(high[axis], partHigh[axis]);
            }
        }
        return found;
    }

    struct GridLine {
        QVector3D a;
        QVector3D b;
        bool major = false;
    };

    /** 受限地面参考网格：格距为自适应毫米步长，范围随可见几何收敛，不再向消失点无限延伸。 */
    QList<GridLine> gridLines() const
    {
        QList<GridLine> result;
        if (!gridVisible || !hasVisibleGeometry() || viewMode == SceneView::ViewMode::ThreeViews) return result;
        const GridStep step = gridStep();
        if (!(step.units > 0) || !(step.millimeters > 0)) return result;
        // 地面几乎与视线平行时（正视/右视正交）重合线会互相叠加变亮且没有判读价值，索性不画。
        QVector3D right, upScreen, forward;
        cameraBasis(right, upScreen, forward);
        if (std::abs(forward.z()) < 0.12f) return result;
        QVector3D low, high;
        if (!visibleDisplayBox(low, high)) return result;
        const QVector3D size = high - low;
        const float modelHalf = std::max(size.x(), size.y()) * 0.5f;
        const int cells = std::clamp(int(std::ceil(modelHalf * 1.6f / step.units)), 2, 29); // 每族≤29，总计≤58
        const float extent = cells * step.units;
        const float centerX = std::round(((low.x() + high.x()) * 0.5f) / step.units) * step.units;
        const float centerY = std::round(((low.y() + high.y()) * 0.5f) / step.units) * step.units;
        const float z = low.z() - std::max(0.02f, size.z() * 0.02f);
        for (int i = -cells; i <= cells; ++i) {
            const float n = i * step.units;
            const bool major = i == 0;
            result.append({QVector3D(centerX + n, centerY - extent, z),
                           QVector3D(centerX + n, centerY + extent, z), major});
            result.append({QVector3D(centerX - extent, centerY + n, z),
                           QVector3D(centerX + extent, centerY + n, z), major});
        }
        return result;
    }

    /** 零件基色：工程视图统一灰白，其余用稳定配色或自定义颜色。 */
    QColor baseColor(const Triangle &triangle) const
    {
        QColor base = colors.value(triangle.id, QColor(89, 157, 205));
        if (viewMode == SceneView::ViewMode::Engineering) base = QColor(175, 184, 195);
        return base;
    }

    /** 指定角点的着色：基色 × 该角点顶点光照；原生路径按角点上传颜色做逐顶点插值。 */
    QColor shadedColor(const Triangle &triangle, int corner) const
    {
        const QColor base = baseColor(triangle);
        const float light = triangle.light[corner];
        return QColor::fromRgbF(base.redF() * light, base.greenF() * light, base.blueF() * light);
    }

    /** 软件回退的逐面着色：基色 × 三角面三个角点光照的平均值。 */
    QColor color(const Triangle &triangle) const
    {
        const QColor base = baseColor(triangle);
        const float light = (triangle.light[0] + triangle.light[1] + triangle.light[2]) / 3;
        return QColor::fromRgbF(base.redF() * light, base.greenF() * light, base.blueF() * light);
    }

    QList<ProjectedFace> projected(const QSize &size) const
    {
        QList<ProjectedFace> faces;
        const auto ports = viewports(size);
        for (int panel = 0; panel < ports.size(); ++panel) {
        const QRect port = ports[panel];
        const QMatrix4x4 mvp = matrix(port.size(), panel);
        for (const auto &triangle : triangles) {
            if (hidden.contains(triangle.id))
                continue;
            ProjectedFace face;
            face.id = triangle.id;
            face.viewport = port;
            face.color = color(triangle);
            const std::array<QVector3D, 3> vertices = {triangle.a, triangle.b, triangle.c};
            bool valid = true;
            const QVector3D offset = offsets.value(triangle.id);
            for (int i = 0; i < 3; ++i) {
                const auto clip = mvp * QVector4D(vertices[i] + offset, 1);
                if (clip.w() <= 0) {
                    valid = false;
                    break;
                }
                const auto ndc = clip.toVector3DAffine();
                if (!finiteNdc(ndc)) { valid = false; break; }
                face.polygon.append(canvasPoint(port, ndc));
                face.depths[i] = ndc.z();
                face.depth += ndc.z() / 3;
            }
            if (valid)
                faces.append(face);
        }
        }
        return faces;
    }

    QString pick(const QPointF &point, const QSize &size) const
    {
        QString nearest;
        double depth = std::numeric_limits<double>::infinity();
        // 以投影三角形的重心坐标插值深度；同名零件仍使用 ID，隐藏对象已被排除。
        for (const auto &face : projected(size)) {
            if (!face.viewport.contains(point.toPoint())) continue;
            const auto a = face.polygon[0], b = face.polygon[1], c = face.polygon[2];
            const double denominator = (b.y() - c.y()) * (a.x() - c.x())
                                     + (c.x() - b.x()) * (a.y() - c.y());
            if (std::abs(denominator) < 1e-8)
                continue;
            const double u = ((b.y() - c.y()) * (point.x() - c.x())
                            + (c.x() - b.x()) * (point.y() - c.y())) / denominator;
            const double v = ((c.y() - a.y()) * (point.x() - c.x())
                            + (a.x() - c.x()) * (point.y() - c.y())) / denominator;
            if (u < 0 || v < 0 || u + v > 1)
                continue;
            const double candidate = u * face.depths[0] + v * face.depths[1]
                                   + (1 - u - v) * face.depths[2];
            if (candidate < depth) {
                depth = candidate;
                nearest = face.id;
            }
        }
        return nearest;
    }

    void overlay(QPainter &painter, const QSize &size) const
    {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(viewMode == SceneView::ViewMode::Engineering ? QColor(45, 55, 65) : QColor(215, 225, 235));
        if (!error.isEmpty())
            painter.drawText(QRect(20, 20, size.width() - 40, size.height() - 40),
                             Qt::AlignCenter | Qt::TextWordWrap, error);
        else if (triangles.isEmpty())
            painter.drawText(QRect(QPoint(), size), Qt::AlignCenter, QStringLiteral("暂无零件"));
        else if (std::all_of(triangles.begin(), triangles.end(),
                            [this](const Triangle &t) { return hidden.contains(t.id); }))
            painter.drawText(QRect(QPoint(), size), Qt::AlignCenter, QStringLiteral("全部零件已隐藏"));
        const auto ports = viewports(size);
        for (int panel = 0; panel < ports.size(); ++panel) {
            const auto port = ports[panel];
            painter.save();
            painter.setClipRect(port);
            const auto mvp = matrix(port.size(), panel);
            const auto project = [&](const QVector3D &p) {
                return canvasPoint(port, (mvp * QVector4D(p, 1)).toVector3DAffine());
            };
            if (gridVisible && hasVisibleGeometry()) {
                const auto step = gridStep();
                if (!gridDrawnInGl) {
                    // 软件回退路径没有深度缓冲，网格仍用真实毫米步长的受限地面范围，低透明度避免遮住实体。
                    painter.setPen(QPen(viewMode == SceneView::ViewMode::Engineering ? QColor(110, 128, 148, 70) : QColor(155, 175, 198, 45), 0.7));
                    for (const auto &line : gridLines()) {
                        const QPointF a = project(line.a), b = project(line.b);
                        if (!std::isfinite(a.x()) || !std::isfinite(a.y())
                            || !std::isfinite(b.x()) || !std::isfinite(b.y())) continue;
                        painter.drawLine(a, b);
                    }
                }
                if (step.millimeters > 0) {
                    painter.setPen(viewMode == SceneView::ViewMode::Engineering ? QColor(80, 95, 110) : QColor(155, 174, 196));
                    // 上移到 -154：右下角方向轴半径 39 像素，避免网格步长文字与方向轴字母重叠。
                    painter.drawText(QRect(port.left() + 8, port.bottom() - 154, port.width() - 16, 20), Qt::AlignRight,
                        QStringLiteral("网格步长 ") + NumericUtil::millimeterText(step.millimeters));
                }
            }
            const auto triad = [&](bool corner) {
                // 画刷/画笔状态必须在本函数内恢复：后面的三视图面板边框用drawRect，会拿当前画刷填充整块面板。
                painter.save();
                // 方向轴用本栏自己的相机基向量，三视图各栏的 XYZ 与所看到的视线一致；
                // 几何与 SceneView::axisGizmo() 共用同一份计算，验收不会与绘制分叉。
                const GizmoGeometry geometry = axisGeometry(corner, port, panel);
                const bool lightBackground = viewMode == SceneView::ViewMode::Engineering;
                drawGizmo(painter, geometry, !corner, lightBackground);
                if (corner) {
                    painter.setPen(lightBackground ? QColor(35, 57, 75) : QColor(198, 215, 228));
                    painter.drawText(QRectF(port.right() - 116, port.bottom() - 28, 108, 18),
                        Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("世界 XYZ"));
                }
                painter.restore();
            };
            if (hasVisibleGeometry() && (axisPlacement == SceneView::AxisPlacement::Corner || axisPlacement == SceneView::AxisPlacement::Both)) triad(true);
            if (hasVisibleGeometry() && (axisPlacement == SceneView::AxisPlacement::Model || axisPlacement == SceneView::AxisPlacement::Both)) triad(false);
            if (viewMode == SceneView::ViewMode::ThreeViews) {
                painter.setPen(QColor(160, 175, 192));
                painter.drawRect(port.adjusted(0, 0, -1, -1));
                painter.drawText(port.x() + 12, 22, panel == 0 ? QStringLiteral("正视图 XZ") : panel == 1 ? QStringLiteral("俯视图 XY") : QStringLiteral("右视图 YZ"));
            }
            painter.restore();
        }
        const auto measurements = annotations(size);
        const QColor background(31, 40, 52, 235);
        QList<QRectF> labels;
        int legendY = viewMode == SceneView::ViewMode::ThreeViews ? 40 : 16;
        for (const auto &measurement : measurements) {
            const QColor color = axisAccentColor(measurement.axis);
            painter.setPen(QPen(color, 1.5));
            if (measurement.lineVisible) {
                painter.drawLine(measurement.start, measurement.end);
                const QLineF line(measurement.start, measurement.end);
                const QPointF perpendicular(-(line.dy()) / line.length() * 4,
                                             line.dx() / line.length() * 4);
                painter.drawLine(measurement.start - perpendicular, measurement.start + perpendicular);
                painter.drawLine(measurement.end - perpendicular, measurement.end + perpendicular);
            }
            if (measurement.axis.isEmpty()) {
                painter.drawText(QPointF(16, size.height() - 72), measurement.label);
                painter.drawText(QPointF(16, size.height() - 40),
                    QStringLiteral("模型中心截面参考比例（垂直视线）"));
                continue;
            }
            const QString legend = measurement.label;
            QRectF legendRect = painter.fontMetrics().boundingRect(legend);
            legendRect.moveTopLeft(QPointF(12, legendY));
            legendRect.adjust(-3, -2, 3, 2);
            painter.drawText(QPointF(12, legendY + painter.fontMetrics().ascent()), legend);
            labels.append(legendRect);
            legendY += painter.fontMetrics().height() + 7;
        }
        // 数值集中在左上避免随旋转相互覆盖；尺寸线以同色单轴字母对应数值。
        for (const auto &measurement : measurements) {
            if (measurement.axis.isEmpty() || !measurement.lineVisible)
                continue;
            QRectF labelRect = painter.fontMetrics().boundingRect(measurement.axis);
            labelRect.moveCenter((measurement.start + measurement.end) / 2);
            labelRect.adjust(-3, -2, 3, 2);
            if (std::any_of(labels.cbegin(), labels.cend(),
                            [&labelRect](const QRectF &other) { return other.intersects(labelRect); }))
                continue;
            painter.fillRect(labelRect, background);
            painter.setPen(axisAccentColor(measurement.axis));
            painter.drawText(labelRect, Qt::AlignCenter, measurement.axis);
            labels.append(labelRect);
        }
        // 三视图每栏很窄：画布内提示会与栏内坐标轴标题重叠，窗口下方已有同一提示。
        if (viewMode != SceneView::ViewMode::ThreeViews) {
            painter.setPen(QColor(215, 225, 235));
            painter.drawText(12, size.height() - 14, QStringLiteral("左键拖动旋转 · 右键拖动平移 · 滚轮缩放 · 短点选择 · 双击空白取消选择"));
        }
    }
};

class SoftwareSurface final : public QWidget {
public:
    SoftwareSurface(SceneData *data, QWidget *parent) : QWidget(parent), data_(data) {}
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), data_->background());
        painter.setRenderHint(QPainter::Antialiasing);
        auto faces = data_->projected(size());
        const bool lines = lineFrameMode(data_->viewMode);
        // 无上下文时采用透明可解释的画家排序；相交几何的遮挡精度低于原生深度缓冲。
        std::stable_sort(faces.begin(), faces.end(),
                         [](const ProjectedFace &a, const ProjectedFace &b) { return a.depth > b.depth; });
        for (const auto &face : faces) {
            if (lines) continue;
            painter.save();
            painter.setClipRect(face.viewport);
            painter.setBrush(face.color);
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(face.polygon);
            painter.restore();
        }
        auto edges = data_->projectedEdges(size());
        std::stable_sort(edges.begin(), edges.end(), [](const ProjectedEdge &a, const ProjectedEdge &b) { return a.depth > b.depth; });
        for (const auto &edge : edges) {
            const bool selected = edge.id == data_->selected;
            if (!selected && !lines && data_->viewMode != SceneView::ViewMode::Engineering) continue;
            painter.save();
            painter.setClipRect(edge.viewport);
            painter.setPen(QPen(selected ? QColor(255, 191, 66) : lines ? QColor(193, 206, 222) : QColor(61, 71, 87), selected ? 1.5 : 0.7));
            painter.drawLine(edge.line);
            painter.restore();
        }
        data_->overlay(painter, size());
    }
private:
    SceneData *data_;
};

class GlSurface final : public QOpenGLWidget, protected QOpenGLExtraFunctions {
public:
    GlSurface(SceneData *data, QWidget *parent) : QOpenGLWidget(parent), data_(data)
    {
        // 控件自身的默认FBO保持单采样：三维绘制进自建FBO，才能独立控制采样数并如实报告实测值。
        QSurfaceFormat requested = format();
        requested.setSamples(0);
        setFormat(requested);
        setContextMenuPolicy(Qt::NoContextMenu); // 右键用于平移，不弹菜单。
    }
    ~GlSurface() override { cleanup(); }
    std::function<void(const QString &)> failed;
    std::function<void()> ready;
protected:
    void initializeGL() override
    {
        QOpenGLExtraFunctions::initializeOpenGLFunctions();
        // 把自建FBO解析到控件FBO需要帧缓冲blit；缺失时退回直接绘制并如实报告，不能假装已超采样。
        blitSupported_ = QOpenGLFramebufferObject::hasOpenGLFramebufferBlit();
        QObject::disconnect(contextDestructionConnection_);
        contextDestructionConnection_ = QObject::connect(
            context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] { cleanup(); },
            Qt::DirectConnection);
        program_ = std::make_unique<QOpenGLShaderProgram>();
        const char *vertex = "attribute vec3 position; attribute vec3 vertexColor; varying vec3 faceColor; uniform mat4 mvp; void main(){faceColor=vertexColor;gl_Position=mvp*vec4(position,1.0);}";
        const char *fragment = "varying vec3 faceColor; uniform float alpha; void main(){gl_FragColor=vec4(faceColor,alpha);}";
        if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex)
            || !program_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment)
            || !program_->link() || !buffer_.create()) {
            if (failed)
                failed(QStringLiteral("OpenGL着色器或缓冲初始化失败：%1").arg(program_->log()));
            return;
        }
        initialized_ = true;
        if (ready)
            ready();
    }

    void paintGL() override
    {
        QPainter painter(this);
        // Qt的OpenGL画家缓存GL状态；显式原生绘制边界才能在自建FBO上恢复正确叠加状态。
        painter.beginNativePainting();
        // 三维绘制进自建FBO：多重采样与超采样都由此控制，解析回控件默认FBO后再画叠加层。
        const bool useTarget = blitSupported_;
        const float ratio = useTarget ? renderScale() : float(std::max(1.0, double(devicePixelRatioF())));
        const QSize pixels(std::max(1, qRound(width() * ratio)), std::max(1, qRound(height() * ratio)));
        if (useTarget) ensureTarget(pixels);
        const bool customTarget = useTarget && target_ && target_->isValid();
        if (customTarget) target_->bind();
        else if (target_) target_.reset();
        // 真正生效的采样数取自本次绘制FBO，而不是请求值。
        const int actualSamples = customTarget ? target_->format().samples() : 0;
        // 自建FBO必须完整（含深度）才能画实体；状态直接进诊断字符串，避免静默画空。
        const QString targetStatus = customTarget
            ? QStringLiteral("，FBO %1x%2 status=0x%3")
                  .arg(target_->width()).arg(target_->height())
                  .arg(int(glCheckFramebufferStatus(GL_FRAMEBUFFER)), 0, 16)
            : QStringLiteral("，使用控件默认FBO");
        data_->renderer = QStringLiteral("原生OpenGL（工程/透视投影与深度缓冲）；MSAA请求4，绘制FBO实际samples=%1，超采样%2x%3")
            .arg(actualSamples).arg(double(customTarget ? ratio : float(devicePixelRatioF())), 0, 'f', 1)
            .arg(actualSamples > 0 ? targetStatus : QStringLiteral("（驱动未提供多重采样FBO）") + targetStatus);
        data_->gridDrawnInGl = true;
        const QColor background = data_->background();
        glClearColor(float(background.redF()), float(background.greenF()), float(background.blueF()), 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (initialized_) {
            glEnable(GL_DEPTH_TEST);
            program_->bind();
            program_->setUniformValue("alpha", 1.0f);
            buffer_.bind();
            QList<float> vertices;
            vertices.reserve(data_->triangles.size() * 18);
            for (const auto &triangle : data_->triangles) {
                if (data_->hidden.contains(triangle.id))
                    continue;
                const QVector3D offset = data_->offsets.value(triangle.id);
                const std::array<QVector3D, 3> corners = {triangle.a, triangle.b, triangle.c};
                // 逐角点颜色：曲面相邻三角面共享顶点法线，插值后不再出现面片台阶。
                for (int corner = 0; corner < 3; ++corner)
                    appendColoredVertex(vertices, corners[corner] + offset,
                                        data_->shadedColor(triangle, corner));
            }
            buffer_.allocate(vertices.constData(), int(vertices.size() * sizeof(float)));
            const int position = program_->attributeLocation("position");
            const int color = program_->attributeLocation("vertexColor");
            program_->enableAttributeArray(position);
            program_->enableAttributeArray(color);
            program_->setAttributeBuffer(position, GL_FLOAT, 0, 3, 6 * sizeof(float));
            program_->setAttributeBuffer(color, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
            const auto ports = data_->viewports(size());
            const bool lines = lineFrameMode(data_->viewMode);
            if (!lines) {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(1, 1);
                drawPanels(ports, ratio, GL_TRIANGLES, int(vertices.size() / 6));
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
            if (lines || data_->viewMode == SceneView::ViewMode::Engineering || !data_->selected.isEmpty()) {
                QList<float> edges;
                edges.reserve(data_->edges.size() * 12);
                for (const auto &edge : data_->edges) {
                    if (data_->hidden.contains(edge.id)) continue;
                    const bool selected = edge.id == data_->selected;
                    if (!selected && !lines && data_->viewMode != SceneView::ViewMode::Engineering) continue;
                    const QColor edgeColor = selected ? QColor(255, 191, 66) : lines ? QColor(193, 206, 222) : QColor(61, 71, 87);
                    const QVector3D offset = data_->offsets.value(edge.id);
                    appendColoredVertex(edges, edge.a + offset, edgeColor);
                    appendColoredVertex(edges, edge.b + offset, edgeColor);
                }
                if (!edges.isEmpty()) {
                    buffer_.allocate(edges.constData(), int(edges.size() * sizeof(float)));
                    drawPanels(ports, ratio, GL_LINES, int(edges.size() / 6));
                }
            }
            // 参考网格用真实深度绘制，被实体遮挡；范围受限，深缩放时不再出现放射状长线。
            const auto gridLineList = data_->gridLines();
            if (!gridLineList.isEmpty()) {
                const bool engineering = data_->viewMode == SceneView::ViewMode::Engineering;
                QList<float> grid;
                grid.reserve(gridLineList.size() * 12);
                for (const auto &line : gridLineList) {
                    const QColor gridColor = engineering
                        ? (line.major ? QColor(118, 136, 158) : QColor(158, 172, 188))
                        : (line.major ? QColor(88, 116, 148) : QColor(66, 88, 114));
                    appendColoredVertex(grid, line.a, gridColor);
                    appendColoredVertex(grid, line.b, gridColor);
                }
                buffer_.allocate(grid.constData(), int(grid.size() * sizeof(float)));
                program_->setUniformValue("alpha", 0.55f);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                drawPanels(ports, ratio, GL_LINES, int(grid.size() / 6));
                program_->setUniformValue("alpha", 1.0f);
                glDisable(GL_BLEND);
            }
            glViewport(0, 0, qRound(width() * ratio), qRound(height() * ratio));
            program_->disableAttributeArray(position);
            program_->disableAttributeArray(color);
            buffer_.release();
            program_->release();
            glDisable(GL_DEPTH_TEST);
        }
        if (customTarget) {
            // 先在相同尺寸下解析多重采样，再用线性缩放写进控件默认FBO，等价网页版的像素比超采样。
            target_->release();
            QOpenGLFramebufferObject::blitFramebuffer(resolve_.get(), QRect(QPoint(0, 0), resolve_->size()),
                target_.get(), QRect(QPoint(0, 0), target_->size()));
            // 目标传 nullptr 表示控件默认FBO；单采样到单采样的缩放拷贝是标准路径。
            QOpenGLFramebufferObject::blitFramebuffer(nullptr,
                QRect(QPoint(0, 0), QSize(std::max(1, qRound(width() * devicePixelRatioF())),
                                          std::max(1, qRound(height() * devicePixelRatioF())))),
                resolve_.get(), QRect(QPoint(0, 0), resolve_->size()), GL_COLOR_BUFFER_BIT, GL_LINEAR);
        }
        // 叠加层必须画在控件自己的FBO上，显式恢复Qt期望的绑定。
        glBindFramebuffer(GL_FRAMEBUFFER, GLuint(defaultFramebufferObject()));
        painter.endNativePainting();
        data_->overlay(painter, size());
    }

private:
    /** 对每个分栏设置视口与MVP后绘制同一批顶点；实体、边与网格三处共用。 */
    void drawPanels(const QList<QRect> &ports, float ratio, GLenum primitive, int vertexCount)
    {
        for (int panel = 0; panel < ports.size(); ++panel) {
            const auto port = ports[panel];
            glViewport(qRound(port.x() * ratio), qRound((height() - port.bottom() - 1) * ratio),
                qRound(port.width() * ratio), qRound(port.height() * ratio));
            program_->setUniformValue("mvp", data_->matrix(port.size(), panel));
            glDrawArrays(primitive, 0, vertexCount);
        }
    }

    /**
     * 超采样倍率：每个逻辑像素至少采样 2×2 个渲染像素（设备像素比更高时直接用它），
     * 与 4 倍多重采样叠加抗锯齿；受 8192 纹理尺寸上限与 2.5 倍上限约束。
     */
    float renderScale() const
    {
        const float dpr = float(std::max(1.0, double(devicePixelRatioF())));
        const float limit = 8192.0f / float(std::max(320, std::max(width(), height())));
        return std::clamp(std::max(dpr, 2.0f), 1.0f, std::max(1.0f, std::min(2.5f, limit)));
    }

    /** 按当前像素尺寸准备绘制FBO；先请求4倍多重采样，驱动不支持时退回单采样并如实报告。 */
    void ensureTarget(const QSize &pixels)
    {
        if (target_ && targetSize_ == pixels)
            return;
        target_.reset();
        targetSize_ = QSize();
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setSamples(4);
        auto candidate = std::make_unique<QOpenGLFramebufferObject>(pixels, format);
        if (!candidate->isValid()) {
            QOpenGLFramebufferObjectFormat plain;
            plain.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
            candidate = std::make_unique<QOpenGLFramebufferObject>(pixels, plain);
        }
        if (!candidate->isValid())
            return; // 连单采样FBO都建不起来时退回控件默认FBO，renderer字符串会如实报告samples=0。
        // 解析缓冲与绘制缓冲同尺寸：跨采样数同时又缩放的blit在部分驱动上不可靠，分两步更稳。
        QOpenGLFramebufferObjectFormat resolved;
        resolved.setAttachment(QOpenGLFramebufferObject::NoAttachment);
        auto resolveTarget = std::make_unique<QOpenGLFramebufferObject>(pixels, resolved);
        if (!resolveTarget->isValid())
            return;
        target_ = std::move(candidate);
        resolve_ = std::move(resolveTarget);
        targetSize_ = pixels;
    }

    void cleanup()
    {
        // Qt可重建上下文；必须在其有效期间释放GPU资源，而非只等外层控件析构。
        // 派生析构之后基类仍可销毁上下文；先断开闭包，避免其访问已销毁的派生成员。
        // 同时允许重建上下文后的initializeGL连接新上下文，避免旧连接重复清理。
        QObject::disconnect(contextDestructionConnection_);
        if (!context())
            return;
        makeCurrent();
        target_.reset();
        resolve_.reset();
        targetSize_ = QSize();
        buffer_.destroy();
        program_.reset();
        initialized_ = false;
        doneCurrent();
    }
    SceneData *data_;
    QMetaObject::Connection contextDestructionConnection_;
    QOpenGLBuffer buffer_;
    std::unique_ptr<QOpenGLShaderProgram> program_;
    std::unique_ptr<QOpenGLFramebufferObject> target_;
    /** 多重采样绘制结果的同尺寸单采样副本，供缩放写进控件FBO。 */
    std::unique_ptr<QOpenGLFramebufferObject> resolve_;
    QSize targetSize_;
    /** 是否能用帧缓冲blit把自建FBO写回控件FBO；不支持时直接绘制并如实报告。 */
    bool blitSupported_ = false;
    bool initialized_ = false;
};

}

struct SceneView::Impl {
    SceneData data;
    QWidget *surface = nullptr;
    QVBoxLayout *layout = nullptr;
    QPoint press;
    QPoint last;
    bool pressing = false;
    /** 右键平移与中键缩放的拖动状态；与左键短点选择互斥。 */
    bool panning = false;
    bool zooming = false;
    bool dragged = false;
    bool doubleClick = false;

    /** 取消全部进行中手势；重建模型、切换视图与焦点丢失共用同一复位。 */
    void resetGestures()
    {
        pressing = dragged = doubleClick = false;
        panning = zooming = false;
    }
};

SceneView::SceneView(QWidget *parent) : QWidget(parent), impl_(std::make_unique<Impl>())
{
    setMinimumSize(240, 180);
    setObjectName(QStringLiteral("sceneView"));
    impl_->layout = new QVBoxLayout(this);
    impl_->layout->setContentsMargins(0, 0, 0, 0);
    const bool software = QApplication::platformName() == QStringLiteral("offscreen")
                       || QApplication::platformName() == QStringLiteral("minimal");
    if (software) {
        impl_->data.renderer = QStringLiteral("软件三维投影（无原生图形上下文；画家遮挡排序）");
        impl_->surface = new SoftwareSurface(&impl_->data, this);
    } else {
        impl_->data.renderer = QStringLiteral("OpenGL初始化中");
        auto *gl = new GlSurface(&impl_->data, this);
        impl_->surface = gl;
        gl->ready = [this] { impl_->data.renderer = QStringLiteral("原生OpenGL；MSAA请求4，绘制FBO采样待首次绘制检测"); };
        auto fallback = [this, gl](const QString &reason) {
            QTimer::singleShot(0, this, [this, gl, reason] {
                if (impl_->surface != gl)
                    return;
                impl_->layout->removeWidget(gl);
                gl->removeEventFilter(this);
                gl->hide();
                auto *surface = new SoftwareSurface(&impl_->data, this);
                impl_->surface = surface;
                impl_->data.renderer = QStringLiteral("软件三维投影回退：%1").arg(reason);
                surface->setObjectName(QStringLiteral("sceneCanvas"));
                surface->installEventFilter(this);
                impl_->layout->addWidget(surface);
                // 回退后旧控件仍借用SceneData；立即释放，避免外层析构后延迟GL清理。
                delete gl;
            });
        };
        gl->failed = fallback;
        // 某些驱动创建上下文失败时不会调用initializeGL，因此另外检测有效性。
        const QPointer<GlSurface> guardedGl(gl);
        QTimer::singleShot(1200, this, [guardedGl, fallback] {
            if (guardedGl && guardedGl->isVisible() && !guardedGl->isValid())
                fallback(QStringLiteral("驱动未能创建有效OpenGL上下文"));
        });
    }
    impl_->surface->setObjectName(QStringLiteral("sceneCanvas"));
    impl_->surface->installEventFilter(this);
    impl_->layout->addWidget(impl_->surface);
}

SceneView::~SceneView()
{
    // 子控件持有SceneData借用指针，必须在PImpl被销毁之前释放它们。
    delete impl_->surface;
}

void SceneView::setParts(const QList<Part> &parts)
{
    // 异步CAD重载取消旧模型的未完成手势，释放事件不能作用于新模型。
    impl_->resetGestures();
    auto &data = impl_->data;
    data.triangles.clear();
    data.edges.clear();
    data.bounds.clear();
    data.millimetersPerUnit = 0;
    data.error.clear();
    data.hidden.clear();
    data.selected.clear();
    data.centers.clear();
    data.offsets.clear();
    data.colors.clear();
    data.defaultColors.clear();
    data.explosionFraction = data.pullFraction = 0;
    data.target = QVector3D();
    long double divisor = 1;
    if (parts.size() > 10000) {
        data.error = QStringLiteral("三维场景超过10000个零件限制");
        fitAll();
        return;
    }
    quint64 meshVertices = 0, meshTriangles = 0;
    QSet<QString> ids;
    for (const auto &part : parts) {
        if (part.id.trimmed().isEmpty() || ids.contains(part.id)) {
            data.error = QStringLiteral("零件ID为空或重复：%1").arg(part.id);
            break;
        }
        ids.insert(part.id);
        // 形状与尺寸规则与仓库保存前校验同一份实现；Scene也能被直接调用，不能只靠导入器。
        if (!PartValidation::hasPositiveShapeDimensions(part)
            || !PartValidation::hasFinitePosition(part)) {
            data.error = QStringLiteral("零件 %1 的三维尺寸或位置无效").arg(part.id);
            break;
        }
        if (part.shape == QStringLiteral("mesh")) {
            QString meshError;
            meshVertices += quint64(part.mesh->vertices.size());
            meshTriangles += quint64(part.mesh->triangles.size());
            // Scene也能被直接调用；不能仅依赖导入器保证索引/有限坐标及资源预算。
            if (meshVertices > MeshGeometryCodec::maxVertices || meshTriangles > MeshGeometryCodec::maxTriangles
                || !PartValidation::validateMeshPart(*part.mesh, {*part.sizeX, *part.sizeY, *part.sizeZ},
                    {part.positionX, part.positionY, part.positionZ}, meshError)) {
                data.error = QStringLiteral("零件 %1 的CAD网格无效：%2").arg(part.id,
                    meshError.isEmpty() ? QStringLiteral("场景网格超过资源限制") : meshError);
                break;
            }
            for (const auto &vertex : part.mesh->vertices)
                for (double value : vertex)
                    divisor = std::max(divisor, std::abs(static_cast<long double>(value)));
        }
        const std::array<double, 8> values = {part.positionX, part.positionY, part.positionZ,
            part.sizeX.value_or(0), part.sizeY.value_or(0), part.sizeZ.value_or(0),
            part.radius.value_or(0), part.length.value_or(0)};
        for (double value : values)
            if (std::isfinite(value))
                divisor = std::max(divisor, std::abs(static_cast<long double>(value)));
    }
    if (!data.error.isEmpty()) {
        fitAll();
        return;
    }
    // 先缩放再求边界，避免DBL_MAX坐标加尺寸、最大值减最小值时溢出。
    std::array<long double, 3> low = {2, 2, 2}, high = {-2, -2, -2};
    for (const auto &part : parts) {
        const std::array<double, 3> positions = {part.positionX, part.positionY, part.positionZ};
        // 先扩展精度再除二，保留合法double次正规尺寸，避免先在double中变成零。
        const std::array<long double, 3> halves = part.shape != QStringLiteral("cylinder")
            ? std::array<long double, 3>{static_cast<long double>(*part.sizeX) / 2,
                static_cast<long double>(*part.sizeY) / 2, static_cast<long double>(*part.sizeZ) / 2}
            : std::array<long double, 3>{static_cast<long double>(*part.length) / 2, *part.radius, *part.radius};
        for (int axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], positions[axis] / divisor - halves[axis] / divisor);
            high[axis] = std::max(high[axis], positions[axis] / divisor + halves[axis] / divisor);
        }
        // 源CAD外包络用于标注；离散曲面的允许公差可能略超出它，取并集保证视角容纳实画顶点。
        if (part.shape == QStringLiteral("mesh")) {
            for (const auto &vertex : part.mesh->vertices) {
                for (int axis = 0; axis < 3; ++axis) {
                    const long double coordinate = positions[axis] / divisor + vertex[axis] / divisor;
                    low[axis] = std::min(low[axis], coordinate);
                    high[axis] = std::max(high[axis], coordinate);
                }
            }
        }
    }
    long double span = 0;
    for (int axis = 0; axis < 3; ++axis)
        span = std::max(span, high[axis] - low[axis]);
    if (!parts.isEmpty() && span <= 0) {
        data.error = QStringLiteral("三维尺寸跨度低于可显示精度，请调整坐标范围");
        fitAll();
        return;
    }
    data.millimetersPerUnit = divisor * span / 2;
    const auto point = [&](const Part &part, long double x, long double y, long double z) {
        const std::array<double, 3> positions = {part.positionX, part.positionY, part.positionZ};
        const std::array<long double, 3> offsets = {x, y, z};
        std::array<float, 3> result;
        for (int axis = 0; axis < 3; ++axis)
            result[axis] = float((positions[axis] / divisor + offsets[axis] / divisor
                              - (low[axis] + high[axis]) / 2) / span * 2);
        return QVector3D(result[0], result[1], result[2]);
    };
    std::array<long double, 3> centerLow = {2, 2, 2}, centerHigh = {-2, -2, -2};
    QSet<QRgb> usedColors;
    const std::array<QColor, 6> palette = {QColor("#3988e8"), QColor("#27bfd0"), QColor("#f28d38"),
        QColor("#ebc54c"), QColor("#9869d9"), QColor("#49b981")};
    for (int i = 0; i < parts.size(); ++i) {
        const auto &part = parts[i];
        data.centers.insert(part.id, point(part, 0, 0, 0));
        const std::array<double, 3> coordinates = {part.positionX, part.positionY, part.positionZ};
        for (int axis = 0; axis < 3; ++axis) {
            centerLow[axis] = std::min(centerLow[axis], coordinates[axis] / divisor);
            centerHigh[axis] = std::max(centerHigh[axis], coordinates[axis] / divisor);
        }
        QColor color = i < int(palette.size()) ? palette[i]
            : QColor::fromHslF(std::fmod(i * 0.381966011250105, 1.0), 0.65, 0.48 + ((i / 12) % 3) * 0.08);
        QRgb rgb = color.rgb() & 0xffffff;
        while (usedColors.contains(rgb)) rgb = (rgb + 1) & 0xffffff;
        usedColors.insert(rgb);
        color = QColor::fromRgb(rgb);
        data.colors.insert(part.id, color);
        data.defaultColors.insert(part.id, color);
    }
    long double maxSpread = -1;
    for (int axis = 0; axis < 3; ++axis) {
        const long double spread = centerHigh[axis] - centerLow[axis];
        if (spread > maxSpread) { maxSpread = spread; data.automaticAxis = axis; }
    }
    if (maxSpread <= 0) {
        long double maximum = -1;
        for (int axis = 0; axis < 3; ++axis) if (high[axis] - low[axis] > maximum) {
            maximum = high[axis] - low[axis]; data.automaticAxis = axis;
        }
    }
    data.updateOffsets();
    // 与渲染相同的float顶点必须仍能表达每件XYZ跨度；否则标线不能声称显示了真实尺寸。
    for (const auto &part : parts) {
        const std::array<long double, 3> dimensions = part.shape != QStringLiteral("cylinder")
            ? std::array<long double, 3>{*part.sizeX, *part.sizeY, *part.sizeZ}
            : std::array<long double, 3>{*part.length, 2 * static_cast<long double>(*part.radius),
                2 * static_cast<long double>(*part.radius)};
        const QVector3D lower = point(part, -dimensions[0] / 2, -dimensions[1] / 2, -dimensions[2] / 2);
        const QVector3D upper = point(part, dimensions[0] / 2, dimensions[1] / 2, dimensions[2] / 2);
        for (int axis = 0; axis < 3; ++axis) {
            const long double expected = dimensions[axis] / data.millimetersPerUnit;
            const long double actual = static_cast<long double>(upper[axis]) - lower[axis];
            if (!std::isfinite(expected) || actual <= 0 || std::abs(actual - expected) / expected > 0.01L) {
                data.error = QStringLiteral("零件 %1 的尺寸跨度低于三维显示精度，请缩小坐标/尺寸范围差异").arg(part.id);
                data.bounds.clear();
                fitAll();
                return;
            }
        }
        data.bounds.append({part.id, part.shape == QStringLiteral("cylinder"), lower, upper, dimensions});
    }
    // 逐顶点光照：平面（长方体、圆柱端面）三角面的三个角点法线相同，
    // 曲面（圆柱侧面、STEP网格）相邻三角面共享平滑法线，避免逐面片台阶。
    const auto triangle = [&](const Part &part, const std::array<QVector3D, 3> &positions,
                              const std::array<QVector3D, 3> &normals) {
        Triangle item{positions[0], positions[1], positions[2], part.id, {}};
        for (int corner = 0; corner < 3; ++corner)
            item.light[corner] = vertexLight(normals[corner]);
        data.triangles.append(item);
    };
    for (const auto &part : parts) {
        const qsizetype firstTriangle = data.triangles.size();
        if (part.shape == QStringLiteral("box")) {
            std::array<QVector3D, 8> vertices;
            for (int i = 0; i < 8; ++i)
                vertices[i] = point(part, (i & 1 ? 1 : -1) * static_cast<long double>(*part.sizeX) / 2,
                                     (i & 2 ? 1 : -1) * static_cast<long double>(*part.sizeY) / 2,
                                     (i & 4 ? 1 : -1) * static_cast<long double>(*part.sizeZ) / 2);
            const int faces[6][4] = {{0,2,3,1},{4,5,7,6},{0,1,5,4},{2,6,7,3},{0,4,6,2},{1,3,7,5}};
            for (const auto &face : faces) {
                const std::array<QVector3D, 4> corners = {vertices[face[0]], vertices[face[1]],
                                                          vertices[face[2]], vertices[face[3]]};
                // 长方体保持分面：每个面用自己的面法线，棱角必须是硬边。
                const QVector3D normal = QVector3D::crossProduct(corners[1] - corners[0],
                                                                corners[2] - corners[0]).normalized();
                triangle(part, {corners[0], corners[1], corners[2]}, {normal, normal, normal});
                triangle(part, {corners[0], corners[2], corners[3]}, {normal, normal, normal});
            }
        } else if (part.shape == QStringLiteral("mesh")) {
            // vertices始终是局部毫米；与基础几何共用position+offset归一化，保留STEP装配位置。
            QList<QVector3D> vertices;
            vertices.reserve(part.mesh->vertices.size());
            for (const auto &vertex : part.mesh->vertices)
                vertices.append(point(part, vertex[0], vertex[1], vertex[2]));
            // 与网页版一致：共享顶点平均法线 → 曲面平滑；超过阈值的硬边仍用面法线。
            const QList<QVector3D> vertexNormals = meshVertexNormals(*part.mesh);
            for (const auto &indices : part.mesh->triangles) {
                const std::array<QVector3D, 3> corners = {vertices[indices[0]], vertices[indices[1]],
                                                          vertices[indices[2]]};
                const QVector3D faceNormal = QVector3D::crossProduct(corners[1] - corners[0],
                                                                    corners[2] - corners[0]).normalized();
                std::array<QVector3D, 3> cornerNormals;
                for (int corner = 0; corner < 3; ++corner) {
                    const QVector3D averaged = vertexNormals[indices[corner]];
                    cornerNormals[corner] = QVector3D::dotProduct(averaged, faceNormal) >= smoothNormalThreshold
                        ? averaged : faceNormal;
                }
                triangle(part, corners, cornerNormals);
            }
        } else if (part.shape == QStringLiteral("cylinder")) {
            constexpr int segments = 20;
            // 乘三角函数前提升半径，防止合法double次正规径向偏移先量化成零或失真后再归一化。
            const long double radius = static_cast<long double>(*part.radius);
            for (int i = 0; i < segments; ++i) {
                const double a = i * 2 * pi / segments, b = (i + 1) * 2 * pi / segments;
                const auto leftA = point(part, -static_cast<long double>(*part.length) / 2, radius * std::cos(a), radius * std::sin(a));
                const auto leftB = point(part, -static_cast<long double>(*part.length) / 2, radius * std::cos(b), radius * std::sin(b));
                const auto rightA = point(part, static_cast<long double>(*part.length) / 2, radius * std::cos(a), radius * std::sin(a));
                const auto rightB = point(part, static_cast<long double>(*part.length) / 2, radius * std::cos(b), radius * std::sin(b));
                // 圆柱轴线沿X：侧面法线为半径方向(0,cos,sin)，端面法线为轴向(±1,0,0)。
                const QVector3D normalA(0, float(std::cos(a)), float(std::sin(a)));
                const QVector3D normalB(0, float(std::cos(b)), float(std::sin(b)));
                const QVector3D capLeft(-1, 0, 0), capRight(1, 0, 0);
                triangle(part, {leftA, rightA, rightB}, {normalA, normalA, normalB});
                triangle(part, {leftA, rightB, leftB}, {normalA, normalB, normalB});
                triangle(part, {point(part, -static_cast<long double>(*part.length) / 2, 0, 0), leftB, leftA},
                         {capLeft, capLeft, capLeft});
                triangle(part, {point(part, static_cast<long double>(*part.length) / 2, 0, 0), rightA, rightB},
                         {capRight, capRight, capRight});
            }
        }
        if (!data.cachePartEdges(firstTriangle)) {
            data.error = QStringLiteral("零件 %1 的边缓存超出三角形资源预算或内存不足").arg(part.id);
            data.edges.clear(); data.triangles.clear(); data.bounds.clear();
            data.centers.clear(); data.offsets.clear(); data.colors.clear(); data.defaultColors.clear();
            data.millimetersPerUnit = 0;
            fitAll();
            return;
        }
    }
    fitAll();
}

void SceneView::setSelectedPart(const QString &id)
{
    const QString selected = impl_->data.centers.contains(id) ? id : QString();
    if (selected != impl_->data.selected) impl_->data.pullFraction = 0;
    impl_->data.selected = selected;
    impl_->data.updateOffsets();
    impl_->surface->update();
}

void SceneView::setHiddenParts(const QSet<QString> &ids)
{
    impl_->data.hidden = ids;
    impl_->surface->update();
}

void SceneView::fitAll()
{
    impl_->data.yaw = 35;
    impl_->data.pitch = impl_->data.viewMode == ViewMode::Isometric ? 25 : 35.264f;
    if (impl_->data.viewMode == ViewMode::Isometric && impl_->data.explosionFraction == 0 && impl_->data.pullFraction == 0) {
        // 保留既有彩色透视“查看全部”相机；可见/选中聚焦用独立入口。
        impl_->data.target = QVector3D();
        const float aspect = float(std::max(1, impl_->surface->width())) / std::max(1, impl_->surface->height());
        impl_->data.distance = std::min(60.0f, 7.2f / std::min(1.0f, aspect));
        impl_->data.minimumDistance = 2.5f;
        impl_->surface->update();
    } else focusVisible();
}

void SceneView::setViewMode(ViewMode mode)
{
    if (mode < ViewMode::Isometric || mode > ViewMode::ThreeViews) return;
    impl_->resetGestures();
    impl_->data.viewMode = mode;
    fitAll();
}

void SceneView::setAxisPlacement(AxisPlacement placement)
{
    if (placement < AxisPlacement::Corner || placement > AxisPlacement::Hidden) return;
    impl_->data.axisPlacement = placement;
    impl_->surface->update();
}

void SceneView::setDimensionsVisible(bool visible) { impl_->data.dimensionsVisible = visible; impl_->surface->update(); }
void SceneView::setGridVisible(bool visible) { impl_->data.gridVisible = visible; impl_->surface->update(); }
void SceneView::setExplodeAxis(ExplodeAxis axis)
{
    if (axis < ExplodeAxis::Auto || axis > ExplodeAxis::Z) return;
    impl_->data.explodeAxis = axis;
    impl_->data.updateOffsets();
    focusVisible();
}

bool SceneView::setExplosionFraction(double fraction)
{
    if (!std::isfinite(fraction) || fraction < 0 || fraction > 1 || !impl_->data.error.isEmpty()) return false;
    impl_->data.explosionFraction = fraction;
    impl_->data.updateOffsets();
    focusVisible();
    return true;
}

bool SceneView::setPullFraction(double fraction)
{
    if (!impl_->data.centers.contains(impl_->data.selected) || !std::isfinite(fraction) || fraction < 0 || fraction > 1
        || !impl_->data.error.isEmpty()) return false;
    impl_->data.pullFraction = fraction;
    impl_->data.updateOffsets();
    focusVisible();
    return true;
}

bool SceneView::setPullDirection(int direction)
{
    if (direction != -1 && direction != 1) return false;
    impl_->data.pullDirection = direction;
    impl_->data.updateOffsets();
    focusVisible();
    return true;
}

void SceneView::restoreDisplayOffsets()
{
    impl_->data.explosionFraction = impl_->data.pullFraction = 0;
    impl_->data.updateOffsets();
    focusVisible();
}

namespace {
bool focusBounds(SceneData &data, const QSize &size, const QString &only = {})
{
    QVector3D low(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    QVector3D high(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
    bool found = false;
    for (const auto &bound : data.bounds) {
        if (data.hidden.contains(bound.id) || (!only.isEmpty() && bound.id != only)) continue;
        found = true;
        for (int axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], bound.low[axis] + data.offsets.value(bound.id)[axis]);
            high[axis] = std::max(high[axis], bound.high[axis] + data.offsets.value(bound.id)[axis]);
        }
    }
    if (!found || !data.error.isEmpty()) return false;
    data.target = (low + high) / 2;
    const float radius = std::max(0.002f, (high - low).length() / 2);
    data.minimumDistance = radius * 1.1f;
    const QSize panel = data.viewports(size).first().size();
    const float aspect = float(std::max(1, panel.width())) / std::max(1, panel.height());
    const float margin = data.viewMode == SceneView::ViewMode::Isometric ? 1 / std::min(1.0f, aspect) : 1.0f;
    data.distance = std::clamp(radius * 3.4f * margin, 0.02f, 60.0f);
    return true;
}
}

void SceneView::focusVisible()
{
    if (!focusBounds(impl_->data, impl_->surface->size())) { impl_->data.target = QVector3D(); impl_->data.distance = 7.2f; }
    impl_->surface->update();
}

bool SceneView::focusSelected()
{
    if (impl_->data.selected.isEmpty() || !focusBounds(impl_->data, impl_->surface->size(), impl_->data.selected)) return false;
    impl_->surface->update();
    return true;
}

bool SceneView::setPartColor(const QString &id, const QColor &color)
{
    if (!impl_->data.colors.contains(id) || !color.isValid()) return false;
    impl_->data.colors.insert(id, QColor(color.red(), color.green(), color.blue()));
    impl_->surface->update();
    return true;
}

bool SceneView::resetPartColor(const QString &id)
{
    return impl_->data.defaultColors.contains(id) && setPartColor(id, impl_->data.defaultColors.value(id));
}

QColor SceneView::partColor(const QString &id) const { return impl_->data.colors.value(id); }
int SceneView::partEdgeCount(const QString &id) const
{
    return int(std::count_if(impl_->data.edges.cbegin(), impl_->data.edges.cend(),
        [&id](const SceneEdge &edge) { return edge.id == id; }));
}
std::array<long double, 3> SceneView::displayOffset(const QString &id) const
{
    const auto offset = impl_->data.offsets.value(id);
    return {offset.x() * impl_->data.millimetersPerUnit, offset.y() * impl_->data.millimetersPerUnit,
            offset.z() * impl_->data.millimetersPerUnit};
}
SceneView::ViewMode SceneView::viewMode() const { return impl_->data.viewMode; }
SceneView::AxisPlacement SceneView::axisPlacement() const { return impl_->data.axisPlacement; }
SceneView::ExplodeAxis SceneView::resolvedExplodeAxis() const { return ExplodeAxis(impl_->data.activeAxis() + 1); }
bool SceneView::dimensionsVisible() const { return impl_->data.dimensionsVisible; }
bool SceneView::gridVisible() const { return impl_->data.gridVisible; }
long double SceneView::gridStepMillimeters() const
{
    return impl_->data.hasVisibleGeometry() ? impl_->data.gridStep().millimeters : 0;
}
int SceneView::gridLineCount() const
{
    // 受限地面网格的实际线数；与渲染同一份几何，关闭网格或三视图时为零。
    return impl_->data.gridLines().size();
}
std::array<long double, 2> SceneView::gridExtentMillimeters() const
{
    const auto lines = impl_->data.gridLines();
    if (lines.isEmpty()) return {0, 0};
    // 任一条线到中心的距离就是方阵半宽，用源毫米比例换算回毫米供核对网格不会无限延伸。
    long double half = 0;
    for (const auto &line : lines)
        half = std::max(half, static_cast<long double>((line.b - line.a).length()) / 2);
    const long double millimeters = half * impl_->data.millimetersPerUnit;
    return std::isfinite(millimeters) ? std::array<long double, 2>{millimeters, millimeters}
                                      : std::array<long double, 2>{0, 0};
}
double SceneView::explosionFraction() const { return impl_->data.explosionFraction; }
double SceneView::pullFraction() const { return impl_->data.pullFraction; }
QVector3D SceneView::cameraTarget() const { return impl_->data.target; }

SceneView::AxisGizmo SceneView::axisGizmo(bool corner, int panel) const
{
    SceneView::AxisGizmo result;
    const QSize size = impl_->surface->size();
    if (size.width() < 240 || size.height() < 180 || !impl_->data.hasVisibleGeometry())
        return result;
    // 三视图按分栏宽度切开画布，与绘制使用同一分栏几何；其它模式只有一栏。
    const auto ports = impl_->data.viewports(size);
    const int index = std::clamp(panel, 0, int(ports.size()) - 1);
    const SceneData::GizmoGeometry geometry =
        impl_->data.axisGeometry(corner, ports[index], index);
    result.origin = geometry.origin;
    result.tips = geometry.tips;
    result.valid = geometry.valid;
    return result;
}

QString SceneView::rendererDescription() const
{
    return impl_->data.error.isEmpty() ? impl_->data.renderer
        : impl_->data.renderer + QStringLiteral("；场景错误：") + impl_->data.error;
}

QList<SceneView::MeasurementAnnotation> SceneView::measurementAnnotations() const
{
    return impl_->data.annotations(impl_->surface->size());
}

bool SceneView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != impl_->surface)
        return QWidget::eventFilter(watched, event);
    if (event->type() == QEvent::Hide || event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate) {
        impl_->resetGestures();
        return QWidget::eventFilter(watched, event);
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            impl_->press = impl_->last = mouse->position().toPoint();
            impl_->pressing = true;
            impl_->panning = false;
            impl_->zooming = false;
            impl_->dragged = false;
            impl_->doubleClick = false;
            return true;
        }
        // 与网页版一致：右键平移、中键缩放；两者都不触发选择，也不能被当成短点。
        if (mouse->button() == Qt::RightButton || mouse->button() == Qt::MiddleButton) {
            impl_->press = impl_->last = mouse->position().toPoint();
            impl_->panning = mouse->button() == Qt::RightButton;
            impl_->zooming = mouse->button() == Qt::MiddleButton;
            impl_->pressing = false;
            impl_->dragged = false;
            impl_->doubleClick = false;
            return true;
        }
    } else if (event->type() == QEvent::MouseMove && (impl_->panning || impl_->zooming)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const QPoint current = mouse->position().toPoint();
        const QPointF delta = QPointF(current - impl_->last);
        if (impl_->panning)
            impl_->data.panByPixels(delta, impl_->surface->size());
        else if (delta.y() != 0)
            impl_->data.zoomBySteps(-delta.y() * 0.01);
        impl_->last = current;
        impl_->surface->update();
        return true;
    } else if (event->type() == QEvent::MouseMove && impl_->pressing) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const QPoint current = mouse->position().toPoint();
        // 一旦超过6像素就保持拖动状态，即使释放时又回到原点也不能触发拾取。
        if ((current - impl_->press).manhattanLength() > 6)
            impl_->dragged = true;
        if (impl_->dragged && (impl_->data.viewMode == ViewMode::Isometric || impl_->data.viewMode == ViewMode::Engineering)) {
            // R07真实反馈：只反转水平方向，保留垂直旋转、短点选和拖动阈值语义。
            impl_->data.yaw -= (current.x() - impl_->last.x()) * 0.5f;
            impl_->data.pitch = std::clamp(impl_->data.pitch + (current.y() - impl_->last.y()) * 0.5f, -85.0f, 85.0f);
            impl_->data.yaw = std::fmod(impl_->data.yaw, 360.0f);
            impl_->surface->update();
        }
        impl_->last = current;
        return true;
    } else if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if ((mouse->button() == Qt::RightButton && impl_->panning)
            || (mouse->button() == Qt::MiddleButton && impl_->zooming)) {
            // 右键/中键拖动结束不产生选择或清选择，也不进入短点判定。
            impl_->panning = impl_->zooming = false;
            impl_->pressing = impl_->dragged = false;
            return true;
        }
        if (mouse->button() == Qt::LeftButton && impl_->pressing) {
            impl_->pressing = false;
            const bool doubleClick = impl_->doubleClick;
            impl_->doubleClick = false;
            if (!impl_->dragged && (mouse->position().toPoint() - impl_->press).manhattanLength() <= 6 && onPartPicked) {
                const QString picked = impl_->data.pick(mouse->position(), impl_->surface->size());
                // 回调可触发窗口销毁；回调后不再访问impl。
                const auto callback = onPartPicked;
                if (!picked.isEmpty() || doubleClick) callback(picked);
            }
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonDblClick) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            // 第二击仍须释放确认；累计拖动越阈值后回原位也不取消选择。
            impl_->press = impl_->last = mouse->position().toPoint();
            impl_->pressing = true;
            impl_->dragged = false;
            impl_->doubleClick = true;
            return true;
        }
    } else if (event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        const float factor = std::exp(-std::clamp(wheel->angleDelta().y(), -1200, 1200) / 1200.0f);
        // 保持相机在包围球之外，防止近裁切面穿过几何导致软件拾取与画面不一致。
        impl_->data.distance = std::clamp(impl_->data.distance * factor, impl_->data.minimumDistance, 60.0f);
        impl_->surface->update();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
