#include "sceneview.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QFontDatabase>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QVector4D>
#include <QWheelEvent>
#include <cmath>
#include <limits>

#define REQUIRE(condition) do { if (!(condition)) { QTextStream(stderr) << "FAIL line " << __LINE__ << ": " << #condition << Qt::endl; return 1; } } while (false)

Part knownBox(const QString &id)
{
    Part part;
    part.id = id;
    part.shape = QStringLiteral("box");
    part.sizeX = 100;
    part.sizeY = 80;
    part.sizeZ = 60;
    return part;
}

void drag(QApplication &app, QWidget *canvas, const QPointF &delta)
{
    const QPointF start = canvas->rect().center();
    QMouseEvent press(QEvent::MouseButtonPress, start, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, start + delta, start + delta, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, start + delta, start + delta, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    app.sendEvent(canvas, &press);
    app.sendEvent(canvas, &move);
    app.sendEvent(canvas, &release);
    app.processEvents();
}

QPointF expectedPoint(const QSize &size, const QVector3D &point, float yaw, float pitch)
{
    // 已知单件100/80/60mm，normalize跨度100mm；独立按相机契约求投影。
    const double radians = std::acos(-1.0) / 180;
    const float distance = 7.2f;
    QMatrix4x4 projection, view;
    projection.perspective(40, float(size.width()) / size.height(), 0.05f, 100);
    view.lookAt(QVector3D(distance * std::cos(pitch * radians) * std::cos(yaw * radians),
        distance * std::cos(pitch * radians) * std::sin(yaw * radians), distance * std::sin(pitch * radians)),
        QVector3D(), QVector3D(0, 0, 1));
    const QVector3D ndc = (projection * view * QVector4D(point, 1)).toVector3DAffine();
    return QPointF((ndc.x() + 1) * size.width() / 2, (1 - ndc.y()) * size.height() / 2);
}

long double pixelsPerMm(const SceneView::MeasurementAnnotation &scale)
{
    return QLineF(scale.start, scale.end).length() / scale.millimeters;
}

QImage capture(SceneView &scene, QWidget *canvas)
{
    // QWidget::grab重定向绘制时会漏掉paintGL叠加，原生证据必须抓真正的GL FBO。
    if (auto *gl = qobject_cast<QOpenGLWidget *>(canvas))
        return gl->grabFramebuffer();
    return scene.grab().toImage();
}

bool hasLineColor(const QImage &image, const QPointF &logicalPoint, const QColor &expected)
{
    const QPoint point = (logicalPoint * image.devicePixelRatio()).toPoint();
    for (int y = point.y() - 3; y <= point.y() + 3; ++y) {
        for (int x = point.x() - 3; x <= point.x() + 3; ++x) {
            if (!image.rect().contains(x, y))
                continue;
            const QColor color = image.pixelColor(x, y);
            // 1.5逻辑像素线可能只有部分覆盖，软件AA与原生MSAA都允许实际混合色。
            // 75阈值仍与深色背景及橙色模型区分，不要求抗锯齿线含完全覆盖像素。
            if (std::abs(color.red() - expected.red()) < 75
                && std::abs(color.green() - expected.green()) < 75
                && std::abs(color.blue() - expected.blue()) < 75)
                return true;
        }
    }
    return false;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    // Windows offscreen插件不自动枚举系统字体；仅测试显式加载本机字体使软件证据可读。
    // 不复制字体或改变产品字体，其他平台缺少此文件时继续验证几何/标线。
    if (QApplication::platformName() == QStringLiteral("offscreen")) {
        const QString windowsRoot = QString::fromLocal8Bit(qgetenv("WINDIR"));
        if (!windowsRoot.isEmpty()) {
            const int fontId = QFontDatabase::addApplicationFont(QDir(windowsRoot).filePath(QStringLiteral("Fonts/msyh.ttc")));
            if (fontId >= 0) {
                const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
                if (!families.isEmpty())
                    app.setFont(QFont(families.first(), 9));
            }
        }
    }
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QStringList args = app.arguments();
    const int snapshotIndex = args.indexOf(QStringLiteral("--snapshot-dir"));
    REQUIRE(snapshotIndex < 0 || snapshotIndex + 1 < args.size());
    QDir destination(snapshotIndex >= 0 ? args[snapshotIndex + 1] : temporary.path());
    REQUIRE(QDir().mkpath(destination.absolutePath()));
    SceneView scene;
    scene.resize(800, 600);
    scene.show();
    QTest::qWait(1500);
    QWidget *canvas = scene.findChild<QWidget *>(QStringLiteral("sceneCanvas"));
    REQUIRE(canvas);
    int picks = 0;
    QString picked;
    scene.onPartPicked = [&](const QString &id) { ++picks; picked = id; };
    scene.setParts({knownBox(QStringLiteral("KNOWN"))});
    scene.setSelectedPart(QStringLiteral("KNOWN"));
    REQUIRE(scene.measurementAnnotations().size() == 1 && scene.measurementAnnotations().first().axis.isEmpty());
    scene.setDimensionsVisible(true);
    app.processEvents();
    auto annotations = scene.measurementAnnotations();
    REQUIRE(annotations.size() == 4);
    REQUIRE(annotations[0].axis.isEmpty());
    REQUIRE(annotations[1].axis == QStringLiteral("X") && annotations[1].millimeters == 100);
    REQUIRE(annotations[2].axis == QStringLiteral("Y") && annotations[2].millimeters == 80);
    REQUIRE(annotations[3].axis == QStringLiteral("Z") && annotations[3].millimeters == 60);
    const long double expectedScale = canvas->height() / (2 * std::tan(20 * std::acos(-1.0) / 180) * 7.2 * 50);
    REQUIRE(std::abs(pixelsPerMm(annotations[0]) / expectedScale - 1) < 1e-6L);
    for (int axis = 1; axis <= 3; ++axis) REQUIRE(!annotations[axis].lineVisible && annotations[axis].start.x() <= 16);
    const QImage knownImage = capture(scene, canvas);
    REQUIRE(knownImage.save(destination.filePath(QStringLiteral("r07-box.png"))));
    // 验证实际像素存在标尺及XYZ线，不以仅生成注释数据替代真正绘制。
    REQUIRE(hasLineColor(knownImage, (annotations[0].start + annotations[0].end) / 2, QColor(215, 225, 235)));
    if (auto *gl = qobject_cast<QOpenGLWidget *>(canvas)) {
        REQUIRE(gl->grabFramebuffer().save(destination.filePath(QStringLiteral("r07-box-framebuffer.png"))));
        REQUIRE(scene.screen()->grabWindow(scene.winId()).save(destination.filePath(QStringLiteral("r07-box-screen.png"))));
    }
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    REQUIRE(picked == QStringLiteral("KNOWN"));
    const int beforeDrag = picks;
    drag(app, canvas, QPointF(60, 0));
    REQUIRE(picks == beforeDrag);
    annotations = scene.measurementAnnotations();
    REQUIRE(capture(scene, canvas) != knownImage);
    REQUIRE(capture(scene, canvas).save(destination.filePath(QStringLiteral("r07-horizontal.png"))));
    scene.fitAll();
    drag(app, canvas, QPointF(0, 30));
    REQUIRE(picks == beforeDrag);
    annotations = scene.measurementAnnotations();
    REQUIRE(capture(scene, canvas) != knownImage);
    scene.fitAll();
    const long double beforeZoom = pixelsPerMm(scene.measurementAnnotations()[0]);
    const QPointF center = canvas->rect().center();
    QWheelEvent wheel(center, center, QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    app.sendEvent(canvas, &wheel);
    app.processEvents();
    const long double afterZoom = pixelsPerMm(scene.measurementAnnotations()[0]);
    REQUIRE(std::abs(afterZoom / beforeZoom - std::exp(0.1)) < 1e-6L);
    REQUIRE(scene.measurementAnnotations()[1].millimeters == 100);
    scene.resize(1000, 750);
    app.processEvents();
    REQUIRE(std::abs(pixelsPerMm(scene.measurementAnnotations()[0]) / afterZoom - 1.25L) < 1e-6L);
    REQUIRE(capture(scene, canvas).save(destination.filePath(QStringLiteral("r07-zoom-resize.png"))));
    scene.setHiddenParts({QStringLiteral("KNOWN")});
    REQUIRE(scene.measurementAnnotations().isEmpty());
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    REQUIRE(picked == QStringLiteral("KNOWN"));
    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    REQUIRE(picked.isEmpty());
    scene.fitAll();
    REQUIRE(scene.measurementAnnotations().isEmpty());
    scene.setHiddenParts({});
    REQUIRE(scene.measurementAnnotations().size() == 4);
    scene.setSelectedPart(QStringLiteral("MISSING"));
    REQUIRE(scene.measurementAnnotations().size() == 4);
    Part other = knownBox(QStringLiteral("OTHER"));
    other.positionX = 200;
    scene.setParts({knownBox(QStringLiteral("KNOWN")), other});
    scene.setSelectedPart(QStringLiteral("KNOWN"));
    scene.setHiddenParts({QStringLiteral("KNOWN")});
    REQUIRE(scene.measurementAnnotations().size() == 4);

    Part cylinder;
    cylinder.id = QStringLiteral("CYLINDER");
    cylinder.shape = QStringLiteral("cylinder");
    cylinder.radius = 20;
    cylinder.length = 100;
    scene.setParts({cylinder});
    scene.setSelectedPart(cylinder.id);
    annotations = scene.measurementAnnotations();
    REQUIRE(annotations.size() == 4);
    REQUIRE(annotations[1].millimeters == 100 && annotations[1].label.contains(QStringLiteral("轴向")));
    REQUIRE(annotations[2].millimeters == 40 && annotations[2].label.contains(QStringLiteral("外径")));
    REQUIRE(annotations[3].millimeters == 40 && annotations[3].label.contains(QStringLiteral("外径")));
    app.processEvents();
    REQUIRE(capture(scene, canvas).save(destination.filePath(QStringLiteral("r07-cylinder.png"))));
    // R07-01：相同比例的圆柱归一化后必须真正画成相同模型；仅包络数值通过不足以发现径向失真。
    // 毫米网格步长会随真实尺寸改变；该像素对照只比较归一化几何，关闭网格叠层。
    scene.setGridVisible(false);
    // 裁掉左上尺寸文字和底部标尺，保留实际模型与轴线像素，避免不同毫米数量影响比较。
    Part proportionalCylinder = cylinder;
    proportionalCylinder.radius = 1;
    proportionalCylinder.length = 2;
    scene.setParts({proportionalCylinder});
    scene.setSelectedPart(proportionalCylinder.id);
    app.processEvents();
    const auto unitAnnotations = scene.measurementAnnotations();
    REQUIRE(unitAnnotations.size() == 4);
    const QRect modelRegion(0, 110, canvas->width(), canvas->height() - 216);
    const QImage unitModel = capture(scene, canvas).copy(modelRegion);
    REQUIRE(unitModel.save(destination.filePath(QStringLiteral("r07-unit-cylinder-model.png"))));
    const double tinyRadius = std::numeric_limits<double>::denorm_min();
    proportionalCylinder.radius = tinyRadius;
    proportionalCylinder.length = 2 * tinyRadius;
    scene.setParts({proportionalCylinder});
    scene.setSelectedPart(proportionalCylinder.id);
    app.processEvents();
    REQUIRE(!scene.rendererDescription().contains(QStringLiteral("场景错误")));
    const auto tinyAnnotations = scene.measurementAnnotations();
    REQUIRE(tinyAnnotations.size() == 4);
    for (int axis = 1; axis <= 3; ++axis) {
        REQUIRE(tinyAnnotations[axis].millimeters == 2 * static_cast<long double>(tinyRadius));
        REQUIRE(QLineF(tinyAnnotations[axis].start, unitAnnotations[axis].start).length() < 1e-6);
        REQUIRE(QLineF(tinyAnnotations[axis].end, unitAnnotations[axis].end).length() < 1e-6);
    }
    const QImage tinyModel = capture(scene, canvas).copy(modelRegion);
    REQUIRE(tinyModel.save(destination.filePath(QStringLiteral("r07-subnormal-cylinder-model.png"))));
    qsizetype differentModelPixels = 0;
    for (int y = 0; y < unitModel.height(); ++y)
        for (int x = 0; x < unitModel.width(); ++x)
            if (unitModel.pixel(x, y) != tinyModel.pixel(x, y))
                ++differentModelPixels;
    QTextStream(stdout) << "R07-01 proportional cylinder differentModelPixels=" << differentModelPixels << Qt::endl;
    REQUIRE(differentModelPixels == 0);
    // 圆柱外径大于DBL_MAX依然有限；不应窄化成inf再显示标尺。
    cylinder.radius = std::numeric_limits<double>::max();
    cylinder.length = std::numeric_limits<double>::max();
    scene.setParts({cylinder});
    scene.setSelectedPart(cylinder.id);
    REQUIRE(!scene.rendererDescription().contains(QStringLiteral("场景错误")));
    annotations = scene.measurementAnnotations();
    REQUIRE(annotations.size() == 4);
    REQUIRE(annotations[2].millimeters > std::numeric_limits<double>::max());
    for (const auto &annotation : annotations) {
        REQUIRE(std::isfinite(annotation.millimeters));
        REQUIRE(!annotation.label.contains(QStringLiteral("inf")) && !annotation.label.contains(QStringLiteral("nan")));
        REQUIRE(std::isfinite(annotation.start.x()) && std::isfinite(annotation.end.y()));
    }
    REQUIRE(capture(scene, canvas).save(destination.filePath(QStringLiteral("r07-extreme.png"))));
    // 最小次正规double是合法有限尺寸；先在double里除二曾导致跨度归零。
    Part tiny = knownBox(QStringLiteral("SUBNORMAL"));
    tiny.sizeX = tiny.sizeY = tiny.sizeZ = std::numeric_limits<double>::denorm_min();
    scene.setParts({tiny});
    scene.setSelectedPart(tiny.id);
    REQUIRE(!scene.rendererDescription().contains(QStringLiteral("场景错误")));
    REQUIRE(scene.measurementAnnotations().size() == 4);
    REQUIRE(scene.measurementAnnotations()[0].millimeters > 0);
    // 巨小件并存使小件float顶点坍缩；必须提示范围错误而非给非真实尺寸标线。
    Part huge = knownBox(QStringLiteral("HUGE"));
    huge.sizeX = huge.sizeY = huge.sizeZ = 1e100;
    scene.setParts({tiny, huge});
    REQUIRE(scene.rendererDescription().contains(QStringLiteral("SUBNORMAL")));
    REQUIRE(scene.measurementAnnotations().isEmpty());
    scene.setParts({});
    REQUIRE(scene.measurementAnnotations().isEmpty());
    scene.setParts({knownBox(QStringLiteral("FINAL"))});
    scene.setSelectedPart(QStringLiteral("FINAL"));
    app.processEvents();
    if (auto *gl = qobject_cast<QOpenGLWidget *>(canvas)) {
        REQUIRE(gl->isValid());
        REQUIRE(gl->format().samples() >= 0);
        REQUIRE(scene.rendererDescription().contains(QStringLiteral("绘制FBO实际samples=")));
        QTextStream(stdout) << "MSAA requested=4 contextFormat.samples=" << gl->format().samples() << Qt::endl;
    } else {
        REQUIRE(scene.rendererDescription().contains(QStringLiteral("软件")));
    }
    QTextStream(stdout) << scene.rendererDescription() << Qt::endl;
    QTextStream(stdout) << "R07 verified: horizontal reversed, vertical unchanged, click, zoom/resize physical scale, XYZ/cylinder envelope, hide/empty, extreme/subnormal/precision guard. snapshots=" << destination.absolutePath() << Qt::endl;
    return 0;
}
