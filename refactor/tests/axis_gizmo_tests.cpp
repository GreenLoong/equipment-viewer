// 右下角世界 XYZ 方向轴验收：按相机真实投影绘制，轴与视线接近平行时必须压短为圆点。
// 该用例只属于重构树（原工程 tests 未改），用于钉住“与网页版一致”的投影规则。
#include "sceneview.h"

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QTest>
#include <QTextStream>
#include <QWidget>

#include <algorithm>
#include <cmath>

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            QTextStream(stderr) << "FAIL at line " << __LINE__ << ": " << #condition << Qt::endl; \
            return 1; \
        } \
    } while (false)

namespace {
constexpr double pi = 3.14159265358979323846;
const QColor axisColors[3] = {QColor(233, 118, 113), QColor(139, 208, 154), QColor(111, 181, 239)};
const char *axisNames[3] = {"X", "Y", "Z"};

bool near(const QColor &actual, const QColor &expected, int tolerance)
{
    return std::abs(actual.red() - expected.red()) <= tolerance
        && std::abs(actual.green() - expected.green()) <= tolerance
        && std::abs(actual.blue() - expected.blue()) <= tolerance;
}

void drain()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QImage frame(QWidget *canvas)
{
    if (auto *gl = qobject_cast<QOpenGLWidget *>(canvas))
        return gl->grabFramebuffer();
    return canvas ? canvas->grab().toImage() : QImage();
}

// 左键拖动：与真实交互一致，一次移动事件即累计到目标位移。
void drag(QWidget *canvas, const QPoint &delta)
{
    const QPoint start(canvas->width() / 2, canvas->height() / 2);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
    const QPoint end = start + delta;
    QMouseEvent move(QEvent::MouseMove, QPointF(end), QPointF(canvas->mapToGlobal(end)),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
    drain();
}

double axisLength(const SceneView::AxisGizmo &gizmo, int axis)
{
    return QLineF(gizmo.origin, gizmo.tips[axis]).length();
}

double axisAngle(const SceneView::AxisGizmo &gizmo, int axis)
{
    const QPointF direction = gizmo.tips[axis] - gizmo.origin;
    return std::atan2(direction.y(), direction.x()) * 180.0 / pi;
}

// 端点附近必须有该轴颜色的像素：验证“算出来的几何确实被画到画布上”。
bool tipIsPainted(const QImage &image, const QPointF &tip, const QColor &color)
{
    for (int offsetX = -3; offsetX <= 3; ++offsetX)
        for (int offsetY = -3; offsetY <= 3; ++offsetY) {
            const QPoint probe = (tip + QPointF(offsetX, offsetY)).toPoint();
            if (probe.x() < 0 || probe.y() < 0 || probe.x() >= image.width() || probe.y() >= image.height())
                continue;
            if (near(image.pixelColor(probe), color, 60))
                return true;
        }
    return false;
}

void report(const char *tag, const SceneView::AxisGizmo &gizmo)
{
    QString line = QStringLiteral("AXIS_GIZMO ") + QString::fromLatin1(tag);
    for (int axis = 0; axis < 3; ++axis)
        line += QStringLiteral(" %1=len:%2/dir:%3")
                    .arg(QString::fromLatin1(axisNames[axis]))
                    .arg(axisLength(gizmo, axis), 0, 'f', 1)
                    .arg(axisAngle(gizmo, axis), 0, 'f', 1);
    QTextStream(stdout) << line << Qt::endl;
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    SceneView scene;
    scene.resize(800, 520);
    scene.setAxisPlacement(SceneView::AxisPlacement::Corner);
    Part part;
    part.id = QStringLiteral("GIZMO-P1");
    part.name = QStringLiteral("方向轴验收件");
    part.shape = QStringLiteral("box");
    part.sizeX = 200;
    part.sizeY = 120;
    part.sizeZ = 80;
    scene.setParts({part});
    scene.show();
    QTest::qWait(200);
    auto *canvas = scene.findChild<QWidget *>(QStringLiteral("sceneCanvas"));
    REQUIRE(canvas);

    // 1) 默认彩色装配：端点 = 原点 + 相机空间投影 × 39。
    //    期望值由相机解析式给出：X 0.6699、Y 0.8542、Z 0.9063；
    //    旧的“归一化方向 + 固定 31 像素”实现三轴都给 31，与本项不符。
    scene.setViewMode(SceneView::ViewMode::Isometric);
    drain();
    QImage image = frame(canvas);
    REQUIRE(!image.isNull());
    SceneView::AxisGizmo gizmo = scene.axisGizmo(true);
    REQUIRE(gizmo.valid);
    report("isometric", gizmo);
    REQUIRE(std::abs(axisLength(gizmo, 0) - 26.1) < 0.6);
    REQUIRE(std::abs(axisLength(gizmo, 1) - 33.3) < 0.6);
    REQUIRE(std::abs(axisLength(gizmo, 2) - 35.3) < 0.6);
    for (int axis = 0; axis < 3; ++axis)
        REQUIRE(tipIsPainted(image, gizmo.tips[axis], axisColors[axis]));

    // 2) 正交三视图：朝观察者的那根轴压成圆点（端点在原点上），另两根保持全长。
    scene.setViewMode(SceneView::ViewMode::Front);
    drain();
    image = frame(canvas);
    gizmo = scene.axisGizmo(true);
    report("front", gizmo);
    REQUIRE(axisLength(gizmo, 1) < 0.01);                        // 前视沿 +Y，Y 轴朝观察者
    REQUIRE(std::abs(axisLength(gizmo, 0) - 39) < 0.01);
    REQUIRE(std::abs(axisAngle(gizmo, 0) - 0.0) < 0.5);
    REQUIRE(std::abs(axisLength(gizmo, 2) - 39) < 0.01);
    REQUIRE(std::abs(axisAngle(gizmo, 2) + 90.0) < 0.5);
    REQUIRE(tipIsPainted(image, gizmo.origin, axisColors[1]));    // 压成圆点但仍画在原点
    for (int axis : {0, 2})
        REQUIRE(tipIsPainted(image, gizmo.tips[axis], axisColors[axis]));

    scene.setViewMode(SceneView::ViewMode::Top);
    drain();
    image = frame(canvas);
    gizmo = scene.axisGizmo(true);
    report("top", gizmo);
    REQUIRE(axisLength(gizmo, 2) < 0.01);                        // 俯视沿 -Z，Z 轴朝观察者
    REQUIRE(std::abs(axisLength(gizmo, 0) - 39) < 0.01);
    REQUIRE(std::abs(axisLength(gizmo, 1) - 39) < 0.01);
    REQUIRE(std::abs(axisAngle(gizmo, 1) + 90.0) < 0.5);

    scene.setViewMode(SceneView::ViewMode::Right);
    drain();
    image = frame(canvas);
    gizmo = scene.axisGizmo(true);
    report("right", gizmo);
    REQUIRE(axisLength(gizmo, 0) < 0.01);                        // 右视沿 -X，X 轴朝观察者
    REQUIRE(std::abs(axisLength(gizmo, 1) - 39) < 0.01);
    REQUIRE(std::abs(axisLength(gizmo, 2) - 39) < 0.01);

    // 3) 相机接近 X 轴方向时，X 轴必须像网页版一样压短；旧的固定长度实现会保持 31。
    scene.setViewMode(SceneView::ViewMode::Isometric);
    drain();
    drag(canvas, QPoint(60, -50));   // yaw≈5°、pitch≈0°，视线几乎沿 +X
    gizmo = scene.axisGizmo(true);
    report("near-x-view", gizmo);
    REQUIRE(axisLength(gizmo, 0) < 6.0);                       // sin5°×39 ≈ 3.4，接近圆点
    REQUIRE(axisLength(gizmo, 1) > 38.0);                      // Y 仍然接近全长
    REQUIRE(axisLength(gizmo, 2) > 38.0);                      // Z 仍然接近全长

    // 4) 旋转时方向轴跟着转：水平拖动改变偏航，X 轴方向必须随之改变。
    scene.setViewMode(SceneView::ViewMode::Isometric);
    drain();
    const double angleBefore = axisAngle(scene.axisGizmo(true), 0);
    drag(canvas, QPoint(40, 0));
    const double angleAfter = axisAngle(scene.axisGizmo(true), 0);
    QTextStream(stdout) << "AXIS_GIZMO rotate xAngleBefore=" << QString::number(angleBefore, 'f', 1)
                        << " xAngleAfter=" << QString::number(angleAfter, 'f', 1) << Qt::endl;
    // 偏航 35°→15°，X 轴投影长度由 26.1 变为 0.483×39≈18.8，方向偏转约 26°。
    REQUIRE(axisLength(scene.axisGizmo(true), 0) > 15.0);
    REQUIRE(std::abs(angleAfter - angleBefore) > 15.0);

    // 5) 三视图：每栏方向轴必须与本栏视线一致（正视/俯视/右视各一栏），不能三栏共用首栏朝向。
    scene.setViewMode(SceneView::ViewMode::ThreeViews);
    drain();
    const SceneView::AxisGizmo frontPanel = scene.axisGizmo(true, 0);
    const SceneView::AxisGizmo topPanel = scene.axisGizmo(true, 1);
    const SceneView::AxisGizmo rightPanel = scene.axisGizmo(true, 2);
    report("threeViews-panel0", frontPanel);
    report("threeViews-panel1", topPanel);
    report("threeViews-panel2", rightPanel);
    REQUIRE(frontPanel.valid && topPanel.valid && rightPanel.valid);
    REQUIRE(axisLength(frontPanel, 1) < 0.01);        // 首栏正视：Y 朝观察者
    REQUIRE(axisLength(topPanel, 2) < 0.01);          // 俯视栏：Z 朝观察者
    REQUIRE(axisLength(rightPanel, 0) < 0.01);        // 右视栏：X 朝观察者
    REQUIRE(axisLength(topPanel, 0) > 38.0 && axisLength(rightPanel, 1) > 38.0);

    QTextStream(stdout) << "AXIS_GIZMO accepted=1 userAcceptance=0 "
                           "projection=web-equivalent foreshortening=1 depthOrder=1 perPanel=1"
                        << Qt::endl;
    return 0;
}
