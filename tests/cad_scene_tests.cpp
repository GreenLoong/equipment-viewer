#include "sceneview.h"
#include "cadresultparser.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QVector4D>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

#define REQUIRE(condition) do { if (!(condition)) { QTextStream(stderr) << "FAIL line " << __LINE__ << ": " << #condition << Qt::endl; return 1; } } while (false)

QImage capture(SceneView &view, QWidget *canvas)
{
    // 原生证据取真实绘制FBO，包括paintGL内的标尺；QWidget::grab会遗漏GL叠加。
    if (auto *gl = qobject_cast<QOpenGLWidget *>(canvas))
        return gl->grabFramebuffer();
    return view.grab().toImage();
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

bool blue(const QColor &color)
{
    return color.blue() > color.red() * 1.6 && color.green() > color.red() * 1.3 && color.blue() > 70;
}

bool orange(const QColor &color)
{
    return color.red() > 100 && color.red() > color.green() * 1.2 && color.green() > color.blue() * 2;
}

QRect blueBounds(const QImage &image)
{
    int left = image.width(), top = image.height(), right = -1, bottom = -1;
    for (int y = 100; y < image.height() - 110; ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (!blue(image.pixelColor(x, y)))
                continue;
            left = std::min(left, x); top = std::min(top, y);
            right = std::max(right, x); bottom = std::max(bottom, y);
        }
    }
    return right < left ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}

// 独立按协议position+localVertex和相机契约计算装配投影范围；隐藏不重新归一化。
QRectF expectedBounds(const QList<Part> &parts, const Part &part, const QSize &size)
{
    std::array<long double, 3> low, high;
    low.fill(std::numeric_limits<long double>::infinity());
    high.fill(-std::numeric_limits<long double>::infinity());
    for (const auto &entry : parts) {
        const std::array<double, 3> pos{entry.positionX, entry.positionY, entry.positionZ};
        const std::array<double, 3> dimensions{*entry.sizeX, *entry.sizeY, *entry.sizeZ};
        for (int axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], pos[axis] - static_cast<long double>(dimensions[axis]) / 2);
            high[axis] = std::max(high[axis], pos[axis] + static_cast<long double>(dimensions[axis]) / 2);
        }
        for (const auto &v : entry.mesh->vertices) {
            for (int axis = 0; axis < 3; ++axis) {
                low[axis] = std::min(low[axis], static_cast<long double>(pos[axis]) + v[axis]);
                high[axis] = std::max(high[axis], static_cast<long double>(pos[axis]) + v[axis]);
            }
        }
    }
    long double span = 0;
    for (int axis = 0; axis < 3; ++axis)
        span = std::max(span, high[axis] - low[axis]);
    QMatrix4x4 projection, view;
    projection.perspective(40, float(size.width()) / size.height(), 0.05f, 100);
    const double rad = std::acos(-1.0) / 180;
    view.lookAt(QVector3D(7.2 * std::cos(25 * rad) * std::cos(35 * rad),
        7.2 * std::cos(25 * rad) * std::sin(35 * rad), 7.2 * std::sin(25 * rad)),
        QVector3D(), QVector3D(0, 0, 1));
    const std::array<double, 3> position{part.positionX, part.positionY, part.positionZ};
    double left = size.width(), top = size.height(), right = 0, bottom = 0;
    for (const auto &face : part.mesh->triangles) {
        for (quint32 index : face) {
            QVector3D point;
            for (int axis = 0; axis < 3; ++axis)
                point[axis] = float((position[axis] + static_cast<long double>(part.mesh->vertices[index][axis])
                    - (low[axis] + high[axis]) / 2) * 2 / span);
            const auto ndc = (projection * view * QVector4D(point, 1)).toVector3DAffine();
            const double x = (ndc.x() + 1) * size.width() / 2;
            const double y = (1 - ndc.y()) * size.height() / 2;
            left = std::min(left, x); top = std::min(top, y);
            right = std::max(right, x); bottom = std::max(bottom, y);
        }
    }
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

bool nearColor(const QImage &image, const QPointF &logical, const QColor &expected)
{
    const QPoint pixel = (logical * image.devicePixelRatio()).toPoint();
    for (int y = pixel.y() - 3; y <= pixel.y() + 3; ++y) {
        for (int x = pixel.x() - 3; x <= pixel.x() + 3; ++x) {
            if (!image.rect().contains(x, y))
                continue;
            const auto color = image.pixelColor(x, y);
            if (std::abs(color.red() - expected.red()) < 75 && std::abs(color.green() - expected.green()) < 75
                && std::abs(color.blue() - expected.blue()) < 75)
                return true;
        }
    }
    return false;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (QApplication::platformName() == QStringLiteral("offscreen")) {
        const QString windowsRoot = QString::fromLocal8Bit(qgetenv("WINDIR"));
        const int fontId = QFontDatabase::addApplicationFont(QDir(windowsRoot).filePath(QStringLiteral("Fonts/msyh.ttc")));
        if (fontId >= 0)
            app.setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).first(), 9));
    }
    const auto args = app.arguments();
    const int probeArg = args.indexOf(QStringLiteral("--probe-dir"));
    REQUIRE(probeArg >= 0 && probeArg + 1 < args.size());
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const int snapshotArg = args.indexOf(QStringLiteral("--snapshot-dir"));
    REQUIRE(snapshotArg < 0 || snapshotArg + 1 < args.size());
    const QDir destination(snapshotArg < 0 ? temporary.path() : args[snapshotArg + 1]);
    REQUIRE(QDir().mkpath(destination.absolutePath()));
    QList<Part> bearing, motor;
    Equipment equipment;
    QString error;
    const QDir probes(args[probeArg + 1]);
    for (const QString &name : {QStringLiteral("bearing"), QStringLiteral("motor")}) {
        QFile source(probes.filePath(name + QStringLiteral(".json")));
        REQUIRE(source.open(QIODevice::ReadOnly));
        auto &parts = name == QStringLiteral("bearing") ? bearing : motor;
        REQUIRE(CadResultParser::parse(source.readAll(), equipment, parts, error));
    }
    REQUIRE(bearing.size() == 1 && motor.size() == 6);
    SceneView scene;
    scene.resize(900, 700);
    scene.show();
    QTest::qWait(1500);
    auto *canvas = scene.findChild<QWidget *>(QStringLiteral("sceneCanvas"));
    REQUIRE(canvas);
    QString picked;
    scene.onPartPicked = [&](const QString &id) { picked = id; };
    scene.setParts(bearing);
    REQUIRE(!scene.rendererDescription().contains(QStringLiteral("场景错误")));
    // 从Z方向观察真实轴承贯通孔；中心背景+空拾取不能由包围盒替代。
    drag(app, canvas, QPointF(0, 120));
    QImage ring = capture(scene, canvas);
    const QPoint center = (QPointF(canvas->rect().center()) * ring.devicePixelRatio()).toPoint();
    REQUIRE(ring.pixelColor(center) == QColor(31, 40, 52));
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    REQUIRE(picked.isEmpty());
    const QRect ringBounds = blueBounds(ring);
    REQUIRE(ringBounds.width() > 150 && ringBounds.height() > 150);
    // 曲面圆轮廓四角露出背景；选择外环后仍保留贯通孔。
    REQUIRE(ring.pixelColor(ringBounds.topLeft() + QPoint(3, 3)) == QColor(31, 40, 52));
    QPoint annulus;
    for (int x = center.x(); x < ringBounds.right(); ++x) {
        if (blue(ring.pixelColor(x, center.y()))) {
            annulus = QPoint(x + 3, center.y());
            break;
        }
    }
    REQUIRE(!annulus.isNull());
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, (QPointF(annulus) / ring.devicePixelRatio()).toPoint());
    REQUIRE(picked == bearing.first().id);
    scene.setSelectedPart(picked);
    app.processEvents();
    const auto measurements = scene.measurementAnnotations();
    REQUIRE(measurements.size() == 4);
    REQUIRE(measurements[1].millimeters == *bearing.first().sizeX);
    REQUIRE(measurements[2].millimeters == *bearing.first().sizeY);
    REQUIRE(measurements[3].millimeters == *bearing.first().sizeZ);
    ring = capture(scene, canvas);
    REQUIRE(ring.pixelColor(center) == QColor(31, 40, 52));
    REQUIRE(orange(ring.pixelColor(annulus)));
    REQUIRE(nearColor(ring, (measurements[0].start + measurements[0].end) / 2, QColor(215, 225, 235)));
    REQUIRE(ring.save(destination.filePath(QStringLiteral("cad-bearing-hole-selected.png"))));
    scene.setHiddenParts({bearing.first().id});
    REQUIRE(scene.measurementAnnotations().isEmpty());
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, (QPointF(annulus) / ring.devicePixelRatio()).toPoint());
    REQUIRE(picked.isEmpty());
    scene.setHiddenParts({});
    REQUIRE(scene.measurementAnnotations().size() == 4);

    scene.setParts(motor);
    app.processEvents();
    REQUIRE(!scene.rendererDescription().contains(QStringLiteral("场景错误")));
    REQUIRE(capture(scene, canvas).save(destination.filePath(QStringLiteral("cad-motor-assembly.png"))));
    for (const auto &part : motor) {
        QSet<QString> hidden;
        for (const auto &other : motor)
            if (other.id != part.id)
                hidden.insert(other.id);
        scene.setHiddenParts(hidden);
        scene.setSelectedPart({});
        app.processEvents();
        const QImage isolated = capture(scene, canvas);
        const QRect observed = blueBounds(isolated);
        const QRectF expected = expectedBounds(motor, part, canvas->size());
        const double dpr = isolated.devicePixelRatio();
        REQUIRE(!observed.isEmpty());
        REQUIRE(std::abs(observed.left() / dpr - expected.left()) < 4);
        REQUIRE(std::abs(observed.right() / dpr - expected.right()) < 4);
        REQUIRE(std::abs(observed.top() / dpr - expected.top()) < 4);
        REQUIRE(std::abs(observed.bottom() / dpr - expected.bottom()) < 4);
        QPoint surfacePoint;
        int closest = std::numeric_limits<int>::max();
        for (int y = observed.top() + 2; y < observed.bottom() - 1; ++y)
            for (int x = observed.left() + 2; x < observed.right() - 1; ++x)
                // MSAA/QPainter边缘混色像素可以在三角外；点选四邻域均为模型的内部像素。
                if (blue(isolated.pixelColor(x, y)) && blue(isolated.pixelColor(x - 1, y))
                    && blue(isolated.pixelColor(x + 1, y)) && blue(isolated.pixelColor(x, y - 1))
                    && blue(isolated.pixelColor(x, y + 1))) {
                    const int distance = (QPoint(x, y) - observed.center()).manhattanLength();
                    if (distance < closest) { surfacePoint = QPoint(x, y); closest = distance; }
                }
        REQUIRE(!surfacePoint.isNull());
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, (QPointF(surfacePoint) / dpr).toPoint());
        QTextStream(stdout) << "pick " << part.id << " at " << surfacePoint.x() << "," << surfacePoint.y()
            << " returned " << picked << "; DPR=" << dpr << Qt::endl;
        REQUIRE(picked == part.id);
        scene.setSelectedPart(part.id);
        REQUIRE(scene.measurementAnnotations().size() == 4);
        REQUIRE(scene.measurementAnnotations()[1].millimeters == *part.sizeX);
        REQUIRE(scene.measurementAnnotations()[2].millimeters == *part.sizeY);
        REQUIRE(scene.measurementAnnotations()[3].millimeters == *part.sizeZ);
        REQUIRE(capture(scene, canvas).save(destination.filePath(part.id + QStringLiteral("-isolated.png"))));
        hidden.insert(part.id);
        scene.setHiddenParts(hidden);
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, (QPointF(surfacePoint) / dpr).toPoint());
        REQUIRE(picked.isEmpty());
        REQUIRE(scene.measurementAnnotations().isEmpty());
        QTextStream(stdout) << part.id << " assembly placement verified; pixel bounds=" << observed.x() << "," << observed.y()
            << "," << observed.width() << "," << observed.height() << Qt::endl;
    }
    scene.setHiddenParts({});
    scene.setSelectedPart(motor.first().id);
    const auto beforeScale = scene.measurementAnnotations()[0];
    const QPointF mouseCenter = canvas->rect().center();
    QWheelEvent wheel(mouseCenter, mouseCenter, QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    app.sendEvent(canvas, &wheel);
    const auto afterScale = scene.measurementAnnotations()[0];
    const long double beforePixels = QLineF(beforeScale.start, beforeScale.end).length() / beforeScale.millimeters;
    const long double afterPixels = QLineF(afterScale.start, afterScale.end).length() / afterScale.millimeters;
    REQUIRE(std::abs(afterPixels / beforePixels - std::exp(0.1)) < 1e-6L);

    // 直接绕过解析器注入坏索引/NaN/缺网格/未知形状，旧CAD和标尺必须清空。
    for (int fault = 0; fault < 4; ++fault) {
        Part invalid = bearing.first();
        auto mutableMesh = std::make_shared<MeshGeometry>(*invalid.mesh);
        if (fault == 0) mutableMesh->triangles[0][0] = quint32(mutableMesh->vertices.size());
        if (fault == 1) mutableMesh->vertices[0][0] = std::numeric_limits<double>::quiet_NaN();
        invalid.mesh = mutableMesh;
        if (fault == 2) invalid.mesh.reset();
        if (fault == 3) invalid.shape = QStringLiteral("unknown");
        scene.setParts({invalid});
        REQUIRE(scene.rendererDescription().contains(QStringLiteral("场景错误")));
        REQUIRE(scene.measurementAnnotations().isEmpty());
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
        REQUIRE(picked.isEmpty());
        REQUIRE(blueBounds(capture(scene, canvas)).isEmpty());
    }
    // 输入所有权释放及重复加载不留Part借用指针；GL与软件路径都必须仍能画孔。
    for (int cycle = 0; cycle < 3; ++cycle) {
        auto host = std::make_unique<QWidget>();
        host->resize(600, 450);
        auto *child = new SceneView(host.get());
        child->setGeometry(host->rect());
        auto disposable = bearing;
        child->setParts(disposable);
        disposable.clear();
        host->show();
        QTest::qWait(100);
        auto *childCanvas = child->findChild<QWidget *>(QStringLiteral("sceneCanvas"));
        REQUIRE(childCanvas);
        REQUIRE(!capture(*child, childCanvas).isNull());
        host.reset();
        app.processEvents();
    }
    scene.setParts(motor);
    scene.setSelectedPart(motor.first().id);
    const QImage finalImage = capture(scene, canvas);
    REQUIRE(finalImage.save(destination.filePath(QStringLiteral("cad-motor-ruler.png"))));
    if (auto *gl = qobject_cast<QOpenGLWidget *>(canvas)) {
        REQUIRE(gl->isValid());
        REQUIRE(scene.rendererDescription().contains(QStringLiteral("绘制FBO实际samples=4")));
    } else {
        REQUIRE(scene.rendererDescription().contains(QStringLiteral("软件")));
    }
    QTextStream(stdout) << scene.rendererDescription() << Qt::endl;
    QTextStream(stdout) << "CAD scene verified: real through-hole/curved silhouette, six world placements, ID/highlight/hide/isolation, exact envelope ruler, zoom, invalid clear, borrowed-data lifecycle. snapshots=" << destination.absolutePath() << Qt::endl;
    return 0;
}
