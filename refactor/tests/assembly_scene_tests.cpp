#include "sceneview.h"

#include <QApplication>
#include <QTest>
#include <QTextStream>
#include <QOpenGLWidget>
#include <QMouseEvent>
#include <QSet>
#include <cmath>
#include <limits>

#define REQUIRE(condition) do { if (!(condition)) { QTextStream(stderr) << "FAIL line " << __LINE__ << ": " << #condition << Qt::endl; return 1; } } while (false)

Part assemblyBox(const QString &id, double x)
{
    Part part;
    part.id = id;
    part.shape = QStringLiteral("box");
    part.sizeX = 10; part.sizeY = 20; part.sizeZ = 30;
    part.positionX = x;
    return part;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    SceneView scene;
    scene.resize(900, 600);
    scene.show();
    QTest::qWait(1500);
    auto *canvas = scene.findChild<QWidget *>(QStringLiteral("sceneCanvas"));
    REQUIRE(canvas);
    QList<Part> source;
    for (int i = 0; i < 82; ++i) source.append(assemblyBox(QString::number(i), (i - 40.5) * 20));
    scene.setParts(source);
    REQUIRE(!scene.rendererDescription().contains(QStringLiteral("场景错误")));
    REQUIRE(!scene.dimensionsVisible() && scene.measurementAnnotations().size() == 1);
    REQUIRE(scene.measurementAnnotations().first().axis.isEmpty());
    REQUIRE(scene.gridStepMillimeters() == 200); // 源X总跨度1630mm，网格步长取真实1/2/5档200mm。
    REQUIRE(scene.gridLineCount() > 0 && scene.gridLineCount() <= 60);
    QSet<QRgb> unique;
    for (const auto &part : source) unique.insert(scene.partColor(part.id).rgb());
    for (const auto &part : source) REQUIRE(scene.partEdgeCount(part.id) == 12); // 长方体只有12条几何棱，没有六面内部对角线。
    REQUIRE(unique.size() == 82);
    REQUIRE(scene.resolvedExplodeAxis() == SceneView::ExplodeAxis::X);
    REQUIRE(scene.setExplosionFraction(1));
    long double previous = -std::numeric_limits<long double>::infinity();
    for (const auto &part : source) {
        const auto offset = scene.displayOffset(part.id);
        REQUIRE(std::isfinite(offset[0]) && offset[1] == 0 && offset[2] == 0);
        REQUIRE((part.positionX < 0 && offset[0] < 0) || (part.positionX > 0 && offset[0] > 0));
        REQUIRE(part.positionX + offset[0] > previous);
        previous = part.positionX + offset[0];
    }
    scene.setSelectedPart(QStringLiteral("0"));
    const auto first = scene.displayOffset(QStringLiteral("0"));
    const auto other = scene.displayOffset(QStringLiteral("1"));
    REQUIRE(scene.setPullFraction(1));
    REQUIRE(scene.displayOffset(QStringLiteral("0"))[0] < first[0]);
    REQUIRE(scene.displayOffset(QStringLiteral("1")) == other);
    REQUIRE(!scene.setPullFraction(std::numeric_limits<double>::quiet_NaN()));
    REQUIRE(!scene.setExplosionFraction(2));
    REQUIRE(!scene.setPullDirection(0));
    const QColor automatic = scene.partColor(QStringLiteral("0"));
    REQUIRE(scene.setPartColor(QStringLiteral("0"), QColor("#ab12cd")));
    scene.restoreDisplayOffsets();
    REQUIRE(scene.partColor(QStringLiteral("0")) == QColor("#ab12cd"));
    REQUIRE(scene.displayOffset(QStringLiteral("0")) == (std::array<long double, 3>{0, 0, 0}));
    REQUIRE(scene.resetPartColor(QStringLiteral("0")) && scene.partColor(QStringLiteral("0")) == automatic);
    REQUIRE(!scene.setPartColor(QStringLiteral("missing"), Qt::red));
    REQUIRE(!scene.setPartColor(QStringLiteral("0"), QColor()));
    scene.setHiddenParts({QStringLiteral("0")});
    REQUIRE(!scene.focusSelected());
    scene.setHiddenParts({});
    REQUIRE(scene.focusSelected());
    scene.setSelectedPart({});
    REQUIRE(!scene.setPullFraction(1));
    for (int i = 0; i < source.size(); ++i) {
        REQUIRE(source[i].positionX == (i - 40.5) * 20);
        REQUIRE(*source[i].sizeX == 10 && *source[i].sizeY == 20 && *source[i].sizeZ == 30);
    }

    scene.setParts({assemblyBox(QStringLiteral("ONE"), 0)});
    scene.setGridVisible(false);
    REQUIRE(scene.gridLineCount() == 0);
    scene.setAxisPlacement(SceneView::AxisPlacement::Hidden);
    int picks = 0;
    QString picked;
    scene.onPartPicked = [&](const QString &id) { ++picks; picked = id; };
    for (auto mode : {SceneView::ViewMode::Isometric, SceneView::ViewMode::Engineering, SceneView::ViewMode::Front,
                      SceneView::ViewMode::Top, SceneView::ViewMode::Right, SceneView::ViewMode::ThreeViews}) {
        scene.setViewMode(mode);
        app.processEvents();
        REQUIRE(scene.viewMode() == mode);
        const int panels = mode == SceneView::ViewMode::ThreeViews ? 3 : 1;
        for (int panel = 0; panel < panels; ++panel) {
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                QPoint((panel * 2 + 1) * (canvas->width() / panels) / 2, canvas->height() / 2));
            REQUIRE(picked == QStringLiteral("ONE"));
        }
    }
    scene.setSelectedPart(QStringLiteral("ONE"));
    scene.setDimensionsVisible(true);
    const auto annotations = scene.measurementAnnotations();
    REQUIRE(annotations.size() == 4);
    REQUIRE(annotations[1].millimeters == 10 && annotations[2].millimeters == 20 && annotations[3].millimeters == 30);
    for (int i = 1; i < annotations.size(); ++i) REQUIRE(!annotations[i].lineVisible && annotations[i].start.x() <= 16);
    scene.setSelectedPart({});
    const auto visibleDimensions = scene.measurementAnnotations();
    REQUIRE(visibleDimensions.size() == 4 && visibleDimensions[1].millimeters == 10
        && visibleDimensions[2].millimeters == 20 && visibleDimensions[3].millimeters == 30);
    REQUIRE(visibleDimensions[1].label.contains(QStringLiteral("可见件")));
    const int before = picks;
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
    REQUIRE(picks == before);
    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
    REQUIRE(picks == before + 1 && picked.isEmpty());
    const int beforeCancelled = picks;
    const QPointF blank(4, 4);
    QMouseEvent pending(QEvent::MouseButtonDblClick, blank, blank, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent pendingRelease(QEvent::MouseButtonRelease, blank, blank, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    app.sendEvent(canvas, &pending);
    scene.setParts({assemblyBox(QStringLiteral("NEW"), 0)});
    app.sendEvent(canvas, &pendingRelease);
    REQUIRE(picks == beforeCancelled);
    app.sendEvent(canvas, &pending);
    scene.setViewMode(SceneView::ViewMode::Front);
    app.sendEvent(canvas, &pendingRelease);
    REQUIRE(picks == beforeCancelled);
    // 第二击拖离后回原位仍是拖动，不能提前/释放清空选择。
    app.sendEvent(canvas, &pending);
    QMouseEvent out(QEvent::MouseMove, blank + QPointF(40, 15), blank + QPointF(40, 15), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent back(QEvent::MouseMove, blank, blank, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    app.sendEvent(canvas, &out);
    app.sendEvent(canvas, &back);
    app.sendEvent(canvas, &pendingRelease);
    REQUIRE(picks == beforeCancelled);
    scene.setGridVisible(true);
    // 地面与视线接近平行时（正视/右视正交）不再画参考网格：重合线互相叠加会变亮，也没有判读价值。
    scene.setViewMode(SceneView::ViewMode::Front);
    REQUIRE(scene.gridLineCount() == 0);
    scene.setViewMode(SceneView::ViewMode::Isometric);
    REQUIRE(scene.gridLineCount() > 0);
    for (double dimension : {std::numeric_limits<double>::max(), std::numeric_limits<double>::denorm_min()}) {
        Part extreme = assemblyBox(QStringLiteral("GRID-EXTREME"), 0);
        extreme.sizeX = extreme.sizeY = extreme.sizeZ = dimension;
        scene.setParts({extreme});
        REQUIRE(!scene.rendererDescription().contains(QStringLiteral("场景错误")));
        REQUIRE(std::isfinite(scene.gridStepMillimeters()) && scene.gridStepMillimeters() > 0);
        REQUIRE(scene.gridLineCount() > 0 && scene.gridLineCount() <= 60);
        const long double decade = std::pow(10.0L, std::floor(std::log10(scene.gridStepMillimeters())));
        const long double mantissa = scene.gridStepMillimeters() / decade;
        REQUIRE(std::abs(mantissa - 1) < 1e-12L || std::abs(mantissa - 2) < 1e-12L || std::abs(mantissa - 5) < 1e-12L);
        REQUIRE(!scene.grab().isNull());
    }
    REQUIRE(!scene.rendererDescription().contains(QStringLiteral("场景错误")));
    // 参考网格必须受限：滚轮放大到多级都不允许网格范围随缩放发散（旧放射状长线的回归）。
    const auto frame = [&]() {
        if (auto *gl = qobject_cast<QOpenGLWidget *>(canvas)) return gl->grabFramebuffer();
        return canvas->grab().toImage();
    };
    scene.setParts({assemblyBox(QStringLiteral("GRID-SCOPE"), 0)});
    const auto wheelZoom = [&](int steps) {
        for (int i = 0; i < steps; ++i) {
            QWheelEvent event(QPointF(canvas->width() / 2.0, canvas->height() / 2.0),
                QPointF(canvas->width() / 2.0, canvas->height() / 2.0), QPoint(), QPoint(0, 120),
                Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(canvas, &event);
        }
        app.processEvents();
    };
    for (int level = 0; level < 3; ++level) {
        wheelZoom(6);
        const auto extent = scene.gridExtentMillimeters();
        REQUIRE(extent[0] > 0 && extent[1] > 0);
        // 网格半宽不得超过“可见几何最大边×1.6 + 一格”，也就是不许随缩放无限延伸。
        REQUIRE(extent[0] <= 20.0L * 1.6L + scene.gridStepMillimeters());
        REQUIRE(extent[0] == extent[1]);
        REQUIRE(scene.gridLineCount() > 0 && scene.gridLineCount() <= 60);
        REQUIRE(!frame().isNull());
    }
    scene.fitAll();
    REQUIRE(scene.gridLineCount() > 0 && scene.gridLineCount() <= 60);

    // 鼠标约定与网页版一致：左键旋转、右键平移（正交视图也可用）、中键缩放；
    // 右键与中键都不得改变选择，也不产生拾取回调。
    scene.setSelectedPart(QStringLiteral("GRID-SCOPE"));
    int gesturePicks = 0;
    scene.onPartPicked = [&](const QString &) { ++gesturePicks; };
    const QPoint gestureCenter(canvas->width() / 2, canvas->height() / 2);
    const auto dragWith = [&](Qt::MouseButton button, const QPoint &delta) {
        QTest::mousePress(canvas, button, Qt::NoModifier, gestureCenter);
        const QPoint end = gestureCenter + delta;
        QMouseEvent move(QEvent::MouseMove, QPointF(end), QPointF(canvas->mapToGlobal(end)),
            Qt::NoButton, button, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
        QTest::mouseRelease(canvas, button, Qt::NoModifier, end);
        app.processEvents();
    };
    const QVector3D beforePan = scene.cameraTarget();
    const QImage beforePanFrame = frame();
    dragWith(Qt::RightButton, QPoint(70, 30));
    REQUIRE(scene.cameraTarget() != beforePan);
    REQUIRE(scene.viewMode() == SceneView::ViewMode::Isometric);
    REQUIRE(gesturePicks == 0);
    REQUIRE(frame() != beforePanFrame);
    // 正交视图允许平移，但不允许旋转。
    scene.setViewMode(SceneView::ViewMode::Front);
    const QVector3D frontBefore = scene.cameraTarget();
    dragWith(Qt::RightButton, QPoint(40, 0));
    REQUIRE(scene.cameraTarget() != frontBefore);
    const QVector3D frontPan = scene.cameraTarget();
    dragWith(Qt::LeftButton, QPoint(60, 20));
    REQUIRE(scene.cameraTarget() == frontPan); // 正视下左键不旋转相机
    // 中键缩放只改相机距离，不动观察点，也不改选择。
    const QVector3D beforeZoom = scene.cameraTarget();
    const QImage beforeZoomFrame = frame();
    // 中键向下拖 = 拉远（与滚轮、网页版 dolly 方向一致）；此时已在上一步放大到上限，只有拉远才有可见变化。
    dragWith(Qt::MiddleButton, QPoint(0, 60));
    REQUIRE(scene.cameraTarget() == beforeZoom);
    REQUIRE(frame() != beforeZoomFrame);
    REQUIRE(gesturePicks == 0);
    // “视角回正”把平移一起复位。
    scene.setViewMode(SceneView::ViewMode::Isometric);
    scene.fitAll();
    REQUIRE(scene.cameraTarget() == QVector3D());
    // 模拟OCCT跨面/三角形重复顶点索引：相同空间坐标仍应焊接成12条棱，源网格不改。
    Part meshBox = assemblyBox(QStringLiteral("EDGE-CUBE"), 0);
    meshBox.shape = QStringLiteral("mesh");
    auto mesh = std::make_shared<MeshGeometry>();
    std::array<std::array<double, 3>, 8> vertices;
    for (int i = 0; i < 8; ++i) vertices[i] = {i & 1 ? 5.0 : -5.0, i & 2 ? 10.0 : -10.0, i & 4 ? 15.0 : -15.0};
    const int faces[6][4] = {{0,2,3,1},{4,5,7,6},{0,1,5,4},{2,6,7,3},{0,4,6,2},{1,3,7,5}};
    for (const auto &face : faces) for (int triangle = 0; triangle < 2; ++triangle) {
        const quint32 start = quint32(mesh->vertices.size());
        mesh->vertices.append(vertices[face[0]]);
        mesh->vertices.append(vertices[face[triangle == 0 ? 1 : 2]]);
        mesh->vertices.append(vertices[face[triangle == 0 ? 2 : 3]]);
        mesh->triangles.append({start, start + 1, start + 2});
    }
    meshBox.mesh = mesh;
    scene.setParts({meshBox});
    REQUIRE(!scene.rendererDescription().contains(QStringLiteral("场景错误")));
    REQUIRE(scene.partEdgeCount(meshBox.id) == 12);
    REQUIRE(mesh->vertices.size() == 36 && mesh->triangles.size() == 12);
    // 公共网格不强制winding；各面一三角反向使共边法向180度，不能按abs(dot)误删。
    auto reversedMesh = std::make_shared<MeshGeometry>(*mesh);
    for (qsizetype i = 0; i < reversedMesh->triangles.size(); i += 2)
        std::swap(reversedMesh->triangles[i][1], reversedMesh->triangles[i][2]);
    Part reversed = meshBox;
    reversed.id = QStringLiteral("REVERSED-WINDING");
    reversed.mesh = reversedMesh;
    scene.setParts({reversed});
    REQUIRE(!scene.rendererDescription().contains(QStringLiteral("场景错误")));
    REQUIRE(scene.partEdgeCount(reversed.id) == 18); // 12条外棱+6条反法向共边均保留。
    scene.setParts({meshBox});
    REQUIRE(scene.partEdgeCount(meshBox.id) == 12);
    scene.setSelectedPart(meshBox.id);
    scene.setExplosionFraction(1); scene.setPullFraction(1);
    REQUIRE(scene.partEdgeCount(meshBox.id) == 12);
    meshBox.sizeX = -1;
    scene.setParts({meshBox});
    REQUIRE(scene.partEdgeCount(QStringLiteral("EDGE-CUBE")) == 0);
    scene.setParts({assemblyBox(QStringLiteral("FINAL"), 0)});
    if (auto *gl = qobject_cast<QOpenGLWidget *>(canvas)) REQUIRE(!gl->grabFramebuffer().isNull());
    else REQUIRE(!scene.grab().isNull());
    QTextStream(stdout) << "Assembly Scene: unique colors, source preservation, ordered offsets, single pull, views/picking, edge dimensions, blank gestures. "
        << scene.rendererDescription() << Qt::endl;
    return 0;
}
