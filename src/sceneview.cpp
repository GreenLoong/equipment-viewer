#include "sceneview.h"

#include <QApplication>
#include <QMatrix4x4>
#include <QMetaObject>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
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
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <iomanip>
#include <sstream>

namespace {
constexpr double pi = 3.14159265358979323846;
struct Triangle {
    QVector3D a, b, c;
    QString id;
    float light = 1;
};

struct ProjectedFace {
    QPolygonF polygon;
    std::array<float, 3> depths;
    QString id;
    QColor color;
    float depth = 0;
};

struct PartBounds {
    QString id;
    bool cylinder = false;
    QVector3D low;
    QVector3D high;
    std::array<long double, 3> millimeters;
};

QString millimeterText(long double value)
{
    // long double 保留归一化计算的范围，不能先窄化 double 再格式化极端外径。
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(5) << value;
    return QString::fromStdString(stream.str()) + QStringLiteral(" mm");
}

struct SceneData {
    QList<Triangle> triangles;
    QSet<QString> hidden;
    QString selected;
    QString error;
    QString renderer;
    QList<PartBounds> bounds;
    long double millimetersPerUnit = 0;
    float yaw = 35;
    float pitch = 25;
    float distance = 4;

    bool hasVisibleGeometry() const
    {
        return error.isEmpty() && std::any_of(triangles.cbegin(), triangles.cend(),
            [this](const Triangle &triangle) { return !hidden.contains(triangle.id); });
    }

    bool projectPoint(const QVector3D &point, const QSize &size, QPointF &projectedPoint) const
    {
        const QVector4D clip = matrix(size) * QVector4D(point, 1);
        if (!std::isfinite(clip.w()) || clip.w() <= 0)
            return false;
        const QVector3D ndc = clip.toVector3DAffine();
        projectedPoint = QPointF((ndc.x() + 1) * size.width() / 2,
                                 (1 - ndc.y()) * size.height() / 2);
        return std::isfinite(projectedPoint.x()) && std::isfinite(projectedPoint.y())
            && ndc.z() >= -1 && ndc.z() <= 1;
    }

    QList<SceneView::MeasurementAnnotation> annotations(const QSize &size) const
    {
        QList<SceneView::MeasurementAnnotation> result;
        if (!hasVisibleGeometry() || size.width() < 240 || size.height() < 180
            || !std::isfinite(millimetersPerUnit) || millimetersPerUnit <= 0)
            return result;
        // 透视相机中心深度为 distance；垂直视线的模型中心截面具有一致比例。
        // 此关系与实际40度FOV、距离、视口和归一化复用，不能用固定像素冒充毫米。
        const long double pixelsPerUnit = size.height() / (2 * std::tan(20 * pi / 180) * distance);
        const long double targetPixels = std::clamp(size.width() * 0.2, 60.0, 150.0);
        const long double targetMm = targetPixels / pixelsPerUnit * millimetersPerUnit;
        const long double decade = std::pow(10.0L, std::floor(std::log10(targetMm)));
        const long double mantissa = targetMm / decade;
        const long double niceMm = (mantissa >= 5 ? 5 : mantissa >= 2 ? 2 : 1) * decade;
        const double pixels = double(niceMm / millimetersPerUnit * pixelsPerUnit);
        if (std::isfinite(pixels) && pixels > 0) {
            result.append({{}, millimeterText(niceMm), niceMm,
                QPointF(16, size.height() - 62), QPointF(16 + pixels, size.height() - 62), true});
        }
        for (const auto &bound : bounds) {
            if (bound.id != selected || hidden.contains(bound.id))
                continue;
            const std::array<QString, 3> axes = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
            for (int axis = 0; axis < 3; ++axis) {
                // 外包络三条相互垂直的边向外挪开，保持数据 X/Y/Z，而非屏幕轴。
                QVector3D a = bound.low - QVector3D(0.08f, 0.08f, 0.08f);
                QVector3D b = a;
                a[axis] = bound.low[axis];
                b[axis] = bound.high[axis];
                QPointF start, end;
                const QRectF safeArea(8, 8, size.width() - 16, size.height() - 106);
                const bool visible = projectPoint(a, size, start) && projectPoint(b, size, end)
                    && safeArea.contains(start) && safeArea.contains(end)
                    && QLineF(start, end).length() >= 24;
                const QString detail = bound.cylinder
                    ? (axis == 0 ? QStringLiteral("（轴向）") : QStringLiteral("（外径）")) : QString();
                result.append({axes[axis], axes[axis] + detail + QStringLiteral(" = ")
                    + millimeterText(bound.millimeters[axis]), bound.millimeters[axis], start, end, visible});
            }
            break;
        }
        return result;
    }

    QMatrix4x4 matrix(const QSize &size) const
    {
        QMatrix4x4 projection, view;
        projection.perspective(40, float(std::max(1, size.width())) / std::max(1, size.height()),
                               0.05f, 100.0f);
        // 数据 Z 向上；以球坐标构建相机，避免把数据轴旋转成 Qt 屏幕轴。
        const float y = yaw * float(pi / 180), p = pitch * float(pi / 180);
        const QVector3D eye(distance * std::cos(p) * std::cos(y),
                            distance * std::cos(p) * std::sin(y), distance * std::sin(p));
        view.lookAt(eye, QVector3D(), QVector3D(0, 0, 1));
        return projection * view;
    }

    QColor color(const Triangle &triangle) const
    {
        const QColor base = triangle.id == selected ? QColor(255, 175, 35) : QColor(89, 157, 205);
        return QColor::fromRgbF(base.redF() * triangle.light, base.greenF() * triangle.light,
                               base.blueF() * triangle.light);
    }

    QList<ProjectedFace> projected(const QSize &size) const
    {
        QList<ProjectedFace> faces;
        const QMatrix4x4 mvp = matrix(size);
        for (const auto &triangle : triangles) {
            if (hidden.contains(triangle.id))
                continue;
            ProjectedFace face;
            face.id = triangle.id;
            face.color = color(triangle);
            const std::array<QVector3D, 3> vertices = {triangle.a, triangle.b, triangle.c};
            bool valid = true;
            for (int i = 0; i < 3; ++i) {
                const auto clip = mvp * QVector4D(vertices[i], 1);
                if (clip.w() <= 0) {
                    valid = false;
                    break;
                }
                const auto ndc = clip.toVector3DAffine();
                face.polygon.append(QPointF((ndc.x() + 1) * size.width() / 2,
                                           (1 - ndc.y()) * size.height() / 2));
                face.depths[i] = ndc.z();
                face.depth += ndc.z() / 3;
            }
            if (valid)
                faces.append(face);
        }
        return faces;
    }

    QString pick(const QPointF &point, const QSize &size) const
    {
        QString nearest;
        double depth = std::numeric_limits<double>::infinity();
        // 以投影三角形的重心坐标插值深度；同名零件仍使用 ID，隐藏对象已被排除。
        for (const auto &face : projected(size)) {
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
        painter.setPen(QColor(215, 225, 235));
        if (!error.isEmpty())
            painter.drawText(QRect(20, 20, size.width() - 40, size.height() - 40),
                             Qt::AlignCenter | Qt::TextWordWrap, error);
        else if (triangles.isEmpty())
            painter.drawText(QRect(QPoint(), size), Qt::AlignCenter, QStringLiteral("暂无零件"));
        else if (std::all_of(triangles.begin(), triangles.end(),
                            [this](const Triangle &t) { return hidden.contains(t.id); }))
            painter.drawText(QRect(QPoint(), size), Qt::AlignCenter, QStringLiteral("全部零件已隐藏"));
        const auto measurements = annotations(size);
        const QColor background(31, 40, 52, 235);
        QList<QRectF> labels;
        int legendY = 16;
        for (const auto &measurement : measurements) {
            const QColor color = measurement.axis == QStringLiteral("X") ? QColor(255, 160, 155)
                : measurement.axis == QStringLiteral("Y") ? QColor(145, 225, 165)
                : measurement.axis == QStringLiteral("Z") ? QColor(150, 190, 255) : QColor(215, 225, 235);
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
            const QString legend = measurement.label + (measurement.lineVisible ? QString()
                : QStringLiteral("；线未显示"));
            QRectF legendRect = painter.fontMetrics().boundingRect(legend);
            legendRect.moveTopLeft(QPointF(12, legendY));
            legendRect.adjust(-3, -2, 3, 2);
            painter.fillRect(legendRect, background);
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
            painter.setPen(measurement.axis == QStringLiteral("X") ? QColor(255, 160, 155)
                : measurement.axis == QStringLiteral("Y") ? QColor(145, 225, 165) : QColor(150, 190, 255));
            painter.drawText(labelRect, Qt::AlignCenter, measurement.axis);
            labels.append(labelRect);
        }
        painter.setPen(QColor(215, 225, 235));
        painter.drawText(12, size.height() - 14, QStringLiteral("左键拖动旋转 · 滚轮缩放 · 短点击选择"));
    }
};

class SoftwareSurface final : public QWidget {
public:
    SoftwareSurface(SceneData *data, QWidget *parent) : QWidget(parent), data_(data) {}
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(31, 40, 52));
        painter.setRenderHint(QPainter::Antialiasing);
        auto faces = data_->projected(size());
        // 无上下文时采用透明可解释的画家排序；相交几何的遮挡精度低于原生深度缓冲。
        std::stable_sort(faces.begin(), faces.end(),
                         [](const ProjectedFace &a, const ProjectedFace &b) { return a.depth > b.depth; });
        for (const auto &face : faces) {
            painter.setBrush(face.color);
            painter.setPen(QPen(face.color.darker(120), 0.5));
            painter.drawPolygon(face.polygon);
        }
        data_->overlay(painter, size());
    }
private:
    SceneData *data_;
};

class GlSurface final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    GlSurface(SceneData *data, QWidget *parent) : QOpenGLWidget(parent), data_(data)
    {
        // 必须在首次创建上下文/FBO前提出请求；驱动仍可降级，不能把请求当作实测。
        QSurfaceFormat requested = format();
        requested.setSamples(4);
        setFormat(requested);
    }
    ~GlSurface() override { cleanup(); }
    std::function<void(const QString &)> failed;
    std::function<void()> ready;
protected:
    void initializeGL() override
    {
        initializeOpenGLFunctions();
        QObject::disconnect(contextDestructionConnection_);
        contextDestructionConnection_ = QObject::connect(
            context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] { cleanup(); },
            Qt::DirectConnection);
        program_ = std::make_unique<QOpenGLShaderProgram>();
        const char *vertex = "attribute vec3 position; attribute vec3 vertexColor; varying vec3 faceColor; uniform mat4 mvp; void main(){faceColor=vertexColor;gl_Position=mvp*vec4(position,1.0);}";
        const char *fragment = "varying vec3 faceColor; void main(){gl_FragColor=vec4(faceColor,1.0);}";
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
        // Qt的OpenGL画家缓存GL状态；显式原生绘制边界才能在MSAA FBO上恢复正确叠加状态。
        painter.beginNativePainting();
        // paintGL进入时Qt已绑定真正的绘制FBO；initializeGL时查询可能仍是默认缓冲。
        GLint actualSamples = 0;
        glGetIntegerv(GL_SAMPLES, &actualSamples);
        data_->renderer = QStringLiteral("原生OpenGL（透视投影与深度缓冲）；MSAA请求4，绘制FBO实际samples=%1%2")
            .arg(actualSamples).arg(actualSamples > 0 ? QString() : QStringLiteral("（驱动未启用多重采样）"));
        glClearColor(31.0f / 255, 40.0f / 255, 52.0f / 255, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (initialized_) {
            glEnable(GL_DEPTH_TEST);
            program_->bind();
            program_->setUniformValue("mvp", data_->matrix(size()));
            buffer_.bind();
            QList<float> vertices;
            vertices.reserve(data_->triangles.size() * 18);
            for (const auto &triangle : data_->triangles) {
                if (data_->hidden.contains(triangle.id))
                    continue;
                const QColor color = data_->color(triangle);
                for (const auto &point : {triangle.a, triangle.b, triangle.c}) {
                    vertices.append(point.x());
                    vertices.append(point.y());
                    vertices.append(point.z());
                    vertices.append(float(color.redF()));
                    vertices.append(float(color.greenF()));
                    vertices.append(float(color.blueF()));
                }
            }
            buffer_.allocate(vertices.constData(), int(vertices.size() * sizeof(float)));
            const int position = program_->attributeLocation("position");
            const int color = program_->attributeLocation("vertexColor");
            program_->enableAttributeArray(position);
            program_->enableAttributeArray(color);
            program_->setAttributeBuffer(position, GL_FLOAT, 0, 3, 6 * sizeof(float));
            program_->setAttributeBuffer(color, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
            glDrawArrays(GL_TRIANGLES, 0, int(vertices.size() / 6));
            program_->disableAttributeArray(position);
            program_->disableAttributeArray(color);
            buffer_.release();
            program_->release();
            glDisable(GL_DEPTH_TEST);
        }
        painter.endNativePainting();
        data_->overlay(painter, size());
    }

private:
    void cleanup()
    {
        // Qt可重建上下文；必须在其有效期间释放GPU资源，而非只等外层控件析构。
        // 派生析构之后基类仍可销毁上下文；先断开闭包，避免其访问已销毁的派生成员。
        // 同时允许重建上下文后的initializeGL连接新上下文，避免旧连接重复清理。
        QObject::disconnect(contextDestructionConnection_);
        if (!context())
            return;
        makeCurrent();
        buffer_.destroy();
        program_.reset();
        initialized_ = false;
        doneCurrent();
    }
    SceneData *data_;
    QMetaObject::Connection contextDestructionConnection_;
    QOpenGLBuffer buffer_;
    std::unique_ptr<QOpenGLShaderProgram> program_;
    bool initialized_ = false;
};

bool positive(const std::optional<double> &value)
{
    return value && std::isfinite(*value) && *value > 0;
}
}

struct SceneView::Impl {
    SceneData data;
    QWidget *surface = nullptr;
    QVBoxLayout *layout = nullptr;
    QPoint press;
    QPoint last;
    bool pressing = false;
    bool dragged = false;
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
    auto &data = impl_->data;
    data.triangles.clear();
    data.bounds.clear();
    data.millimetersPerUnit = 0;
    data.error.clear();
    data.hidden.clear();
    data.selected.clear();
    long double divisor = 1;
    if (parts.size() > 10000) {
        data.error = QStringLiteral("三维场景超过10000个零件限制");
        fitAll();
        return;
    }
    quint64 meshVertices = 0, meshTriangles = 0;
    for (const auto &part : parts) {
        const bool solidDimensions = positive(part.sizeX) && positive(part.sizeY) && positive(part.sizeZ);
        const bool shapeValid = part.shape == QStringLiteral("box")
            ? solidDimensions
            : part.shape == QStringLiteral("mesh") ? solidDimensions && bool(part.mesh)
            : part.shape == QStringLiteral("cylinder") && positive(part.radius) && positive(part.length);
        if (!shapeValid || !std::isfinite(part.positionX) || !std::isfinite(part.positionY)
            || !std::isfinite(part.positionZ)) {
            data.error = QStringLiteral("零件 %1 的三维尺寸或位置无效").arg(part.id);
            break;
        }
        if (part.shape == QStringLiteral("mesh")) {
            QString meshError;
            meshVertices += quint64(part.mesh->vertices.size());
            meshTriangles += quint64(part.mesh->triangles.size());
            // Scene也能被直接调用；不能仅依赖导入器保证索引/有限坐标及资源预算。
            if (meshVertices > MeshGeometryCodec::maxVertices || meshTriangles > MeshGeometryCodec::maxTriangles
                || !MeshGeometryCodec::validate(*part.mesh, meshError)
                || !MeshGeometryCodec::validateBounds(*part.mesh, {*part.sizeX, *part.sizeY, *part.sizeZ},
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
    const auto triangle = [&](const Part &part, QVector3D a, QVector3D b, QVector3D c) {
        const QVector3D normal = QVector3D::crossProduct(b - a, c - a).normalized();
        const float light = 0.55f + 0.45f * std::abs(QVector3D::dotProduct(normal, QVector3D(0.3f, 0.4f, 0.86f)));
        data.triangles.append({a, b, c, part.id, light});
    };
    for (const auto &part : parts) {
        if (part.shape == QStringLiteral("box")) {
            std::array<QVector3D, 8> vertices;
            for (int i = 0; i < 8; ++i)
                vertices[i] = point(part, (i & 1 ? 1 : -1) * static_cast<long double>(*part.sizeX) / 2,
                                     (i & 2 ? 1 : -1) * static_cast<long double>(*part.sizeY) / 2,
                                     (i & 4 ? 1 : -1) * static_cast<long double>(*part.sizeZ) / 2);
            const int faces[6][4] = {{0,2,3,1},{4,5,7,6},{0,1,5,4},{2,6,7,3},{0,4,6,2},{1,3,7,5}};
            for (const auto &face : faces) {
                triangle(part, vertices[face[0]], vertices[face[1]], vertices[face[2]]);
                triangle(part, vertices[face[0]], vertices[face[2]], vertices[face[3]]);
            }
        } else if (part.shape == QStringLiteral("mesh")) {
            // vertices始终是局部毫米；与基础几何共用position+offset归一化，保留STEP装配位置。
            QList<QVector3D> vertices;
            vertices.reserve(part.mesh->vertices.size());
            for (const auto &vertex : part.mesh->vertices)
                vertices.append(point(part, vertex[0], vertex[1], vertex[2]));
            for (const auto &indices : part.mesh->triangles)
                triangle(part, vertices[indices[0]], vertices[indices[1]], vertices[indices[2]]);
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
                triangle(part, leftA, rightA, rightB);
                triangle(part, leftA, rightB, leftB);
                triangle(part, point(part, -static_cast<long double>(*part.length) / 2, 0, 0), leftB, leftA);
                triangle(part, point(part, static_cast<long double>(*part.length) / 2, 0, 0), rightA, rightB);
            }
        }
    }
    fitAll();
}

void SceneView::setSelectedPart(const QString &id)
{
    impl_->data.selected = id;
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
    impl_->data.pitch = 25;
    // 归一化后包围盒在[-1,1]内；窄视口需更远距离才能完整容纳。
    const float aspect = float(std::max(1, impl_->surface->width())) / std::max(1, impl_->surface->height());
    impl_->data.distance = std::min(60.0f, 7.2f / std::min(1.0f, aspect));
    impl_->surface->update();
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
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            impl_->press = impl_->last = mouse->position().toPoint();
            impl_->pressing = true;
            impl_->dragged = false;
            return true;
        }
    } else if (event->type() == QEvent::MouseMove && impl_->pressing) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const QPoint current = mouse->position().toPoint();
        // 一旦超过6像素就保持拖动状态，即使释放时又回到原点也不能触发拾取。
        if ((current - impl_->press).manhattanLength() > 6)
            impl_->dragged = true;
        if (impl_->dragged) {
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
        if (mouse->button() == Qt::LeftButton && impl_->pressing) {
            impl_->pressing = false;
            if (!impl_->dragged && (mouse->position().toPoint() - impl_->press).manhattanLength() <= 6 && onPartPicked)
                onPartPicked(impl_->data.pick(mouse->position(), impl_->surface->size()));
            return true;
        }
    } else if (event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        const float factor = std::exp(-std::clamp(wheel->angleDelta().y(), -1200, 1200) / 1200.0f);
        // 保持相机在包围球之外，防止近裁切面穿过几何导致软件拾取与画面不一致。
        impl_->data.distance = std::clamp(impl_->data.distance * factor, 2.5f, 60.0f);
        impl_->surface->update();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
