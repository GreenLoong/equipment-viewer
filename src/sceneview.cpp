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
#include <QTimer>
#include <QVBoxLayout>
#include <QVector3D>
#include <QVector4D>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

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

struct SceneData {
    QList<Triangle> triangles;
    QSet<QString> hidden;
    QString selected;
    QString error;
    QString renderer;
    float yaw = 35;
    float pitch = 25;
    float distance = 4;

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
        painter.setPen(QColor(215, 225, 235));
        if (!error.isEmpty())
            painter.drawText(QRect(20, 20, size.width() - 40, size.height() - 40),
                             Qt::AlignCenter | Qt::TextWordWrap, error);
        else if (triangles.isEmpty())
            painter.drawText(QRect(QPoint(), size), Qt::AlignCenter, QStringLiteral("暂无零件"));
        else if (std::all_of(triangles.begin(), triangles.end(),
                            [this](const Triangle &t) { return hidden.contains(t.id); }))
            painter.drawText(QRect(QPoint(), size), Qt::AlignCenter, QStringLiteral("全部零件已隐藏"));
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
    GlSurface(SceneData *data, QWidget *parent) : QOpenGLWidget(parent), data_(data) {}
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
        QPainter painter(this);
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
        gl->ready = [this] { impl_->data.renderer = QStringLiteral("原生OpenGL（透视投影与深度缓冲）"); };
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
                gl->deleteLater();
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
    data.error.clear();
    data.hidden.clear();
    data.selected.clear();
    long double divisor = 1;
    if (parts.size() > 10000)
        data.error = QStringLiteral("三维场景超过10000个零件限制");
    for (const auto &part : parts) {
        const bool shapeValid = part.shape == QStringLiteral("box")
            ? positive(part.sizeX) && positive(part.sizeY) && positive(part.sizeZ)
            : part.shape == QStringLiteral("cylinder") && positive(part.radius) && positive(part.length);
        if (!shapeValid || !std::isfinite(part.positionX) || !std::isfinite(part.positionY)
            || !std::isfinite(part.positionZ)) {
            data.error = QStringLiteral("零件 %1 的三维尺寸或位置无效").arg(part.id);
            break;
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
        const std::array<double, 3> halves = part.shape == QStringLiteral("box")
            ? std::array<double, 3>{*part.sizeX / 2, *part.sizeY / 2, *part.sizeZ / 2}
            : std::array<double, 3>{*part.length / 2, *part.radius, *part.radius};
        for (int axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], positions[axis] / divisor - halves[axis] / divisor);
            high[axis] = std::max(high[axis], positions[axis] / divisor + halves[axis] / divisor);
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
    const auto point = [&](const Part &part, double x, double y, double z) {
        const std::array<double, 3> positions = {part.positionX, part.positionY, part.positionZ};
        const std::array<double, 3> offsets = {x, y, z};
        std::array<float, 3> result;
        for (int axis = 0; axis < 3; ++axis)
            result[axis] = float((positions[axis] / divisor + offsets[axis] / divisor
                              - (low[axis] + high[axis]) / 2) / span * 2);
        return QVector3D(result[0], result[1], result[2]);
    };
    const auto triangle = [&](const Part &part, QVector3D a, QVector3D b, QVector3D c) {
        const QVector3D normal = QVector3D::crossProduct(b - a, c - a).normalized();
        const float light = 0.55f + 0.45f * std::abs(QVector3D::dotProduct(normal, QVector3D(0.3f, 0.4f, 0.86f)));
        data.triangles.append({a, b, c, part.id, light});
    };
    for (const auto &part : parts) {
        if (part.shape == QStringLiteral("box")) {
            std::array<QVector3D, 8> vertices;
            for (int i = 0; i < 8; ++i)
                vertices[i] = point(part, (i & 1 ? 1 : -1) * *part.sizeX / 2,
                                     (i & 2 ? 1 : -1) * *part.sizeY / 2,
                                     (i & 4 ? 1 : -1) * *part.sizeZ / 2);
            const int faces[6][4] = {{0,2,3,1},{4,5,7,6},{0,1,5,4},{2,6,7,3},{0,4,6,2},{1,3,7,5}};
            for (const auto &face : faces) {
                triangle(part, vertices[face[0]], vertices[face[1]], vertices[face[2]]);
                triangle(part, vertices[face[0]], vertices[face[2]], vertices[face[3]]);
            }
        } else {
            constexpr int segments = 20;
            for (int i = 0; i < segments; ++i) {
                const double a = i * 2 * pi / segments, b = (i + 1) * 2 * pi / segments;
                const auto leftA = point(part, -*part.length / 2, *part.radius * std::cos(a), *part.radius * std::sin(a));
                const auto leftB = point(part, -*part.length / 2, *part.radius * std::cos(b), *part.radius * std::sin(b));
                const auto rightA = point(part, *part.length / 2, *part.radius * std::cos(a), *part.radius * std::sin(a));
                const auto rightB = point(part, *part.length / 2, *part.radius * std::cos(b), *part.radius * std::sin(b));
                triangle(part, leftA, rightA, rightB);
                triangle(part, leftA, rightB, leftB);
                triangle(part, point(part, -*part.length / 2, 0, 0), leftB, leftA);
                triangle(part, point(part, *part.length / 2, 0, 0), rightA, rightB);
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
            impl_->data.yaw += (current.x() - impl_->last.x()) * 0.5f;
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
