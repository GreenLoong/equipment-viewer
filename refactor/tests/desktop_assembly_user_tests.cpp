#include "equipmentcontroller.h"
#include "equipmentwindow.h"
#include "equipmentrepository.h"
#include "meshgeometry.h"
#include "sceneview.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QColorDialog>
#include <QDialog>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QPixmap>
#include <QSlider>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <cmath>
#include <array>
#include <limits>

// 独立装配流程回归：使用临时SQLite及真实控件事件，不操作正常数据库/后台服务。
namespace {
#define REQUIRE(condition) do { if (!(condition)) { QTextStream(stderr) << "DESKTOP_ASSEMBLY_FAIL " << __LINE__ << ": " << #condition << Qt::endl; return false; } } while (false)

const QString equipmentId = QStringLiteral("INDEPENDENT-ASM");
const QString aId = QStringLiteral("A");
const QString bId = QStringLiteral("B ");
const QString cId = QStringLiteral("C");
QString snapshots;
void drain();

bool snapshot(QWidget &window, const QString &name)
{
    if (snapshots.isEmpty()) return true;
    drain();
    if (QApplication::platformName() == QStringLiteral("windows")) {
        // 只取本次测试窗口，避免QWidget::grab漏掉原生OpenGL子画布。
        window.raise(); window.activateWindow(); QTest::qWait(60);
        if (!window.screen()) return false;
        const auto image = window.screen()->grabWindow(window.winId());
        return !image.isNull() && image.save(QDir(snapshots).filePath(name));
    }
    return window.grab().save(QDir(snapshots).filePath(name));
}

QByteArray fixture()
{
    QJsonArray parts;
    const QStringList ids{aId, bId, cId};
    const QList<double> positions{-120, 0, 120};
    for (int i = 0; i < ids.size(); ++i) {
        parts.append(QJsonObject{{"id", ids[i]}, {"name", QStringLiteral("同名零件")},
            {"material", QStringLiteral("独立测试材料")}, {"shape", "box"},
            {"dimensions", QJsonArray{40, 34, 28}}, {"position", QJsonArray{positions[i], 0, 0}}});
    }
    return QJsonDocument(QJsonObject{{"equipmentId", equipmentId},
        {"equipmentName", QStringLiteral("独立装配回归样本")}, {"parts", parts}}).toJson(QJsonDocument::Compact);
}

void drain()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

bool sameSources(const QList<Part> &before, const QList<Part> &after)
{
    if (before.size() != after.size()) return false;
    for (int i = 0; i < before.size(); ++i) {
        const auto &a = before[i]; const auto &b = after[i];
        if (a.equipmentId != b.equipmentId || a.id != b.id || a.name != b.name || a.material != b.material
            || a.shape != b.shape || a.positionX != b.positionX || a.positionY != b.positionY
            || a.positionZ != b.positionZ || a.sizeX != b.sizeX || a.sizeY != b.sizeY || a.sizeZ != b.sizeZ
            || a.radius != b.radius || a.length != b.length || a.cadSourceSha256 != b.cadSourceSha256)
            return false;
        if (bool(a.mesh) != bool(b.mesh)) return false;
        if (a.mesh && (a.mesh->vertices != b.mesh->vertices || a.mesh->triangles != b.mesh->triangles)) return false;
    }
    return true;
}

bool clickRow(QListWidget *list, const QString &id)
{
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->data(Qt::UserRole).toString() != id) continue;
        list->scrollToItem(list->item(row));
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(row)).center());
        drain();
        return true;
    }
    return false;
}

bool choose(QComboBox *combo, int data)
{
    const int index = combo->findData(data);
    if (index < 0) return false;
    combo->setFocus(); QTest::keyClick(combo, Qt::Key_Home);
    for (int step = 0; step < index; ++step) QTest::keyClick(combo, Qt::Key_Down);
    drain();
    return combo->currentData().toInt() == data;
}

bool colorChoice(QPushButton *button, QWidget &owner, const QColor &color, bool accept)
{
    bool observed = false;
    QTimer poll, deadline;
    poll.setInterval(10); deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &owner, [&] {
        if (auto *dialog = qobject_cast<QColorDialog *>(QApplication::activeModalWidget())) {
            observed = true; poll.stop();
            if (accept) { dialog->setCurrentColor(color); dialog->accept(); }
            else dialog->reject();
        }
    });
    // 新UI若打开意外模态窗，受控退出使错误成为测试失败，而不是永远挂住构建验证。
    QObject::connect(&deadline, &QTimer::timeout, &owner, [&] {
        poll.stop();
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
    });
    poll.start(); deadline.start(2000); QTest::mouseClick(button, Qt::LeftButton);
    poll.stop(); deadline.stop();
    return observed;
}

void doubleClick(QWidget *canvas, const QPoint &point)
{
    // 完整第一击加Qt双击/释放；不能直接调用业务回调伪造鼠标路径。
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, point);
    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, point);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, point);
    drain();
}

void roundTripDrag(QWidget *canvas, const QPoint &start)
{
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
    for (const QPoint &point : {start + QPoint(35, 0), start}) {
        QMouseEvent move(QEvent::MouseMove, QPointF(point), QPointF(canvas->mapToGlobal(point)),
            Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
    }
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, start);
    drain();
}

void doubleSecondDrag(QWidget *canvas, const QPoint &start)
{
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, start);
    for (const QPoint &point : {start + QPoint(35, 0), start}) {
        QMouseEvent move(QEvent::MouseMove, QPointF(point), QPointF(canvas->mapToGlobal(point)),
            Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
    }
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, start);
    drain();
}

QImage pixels(QWidget *canvas)
{
    if (auto *native = dynamic_cast<QOpenGLWidget *>(canvas)) return native->grabFramebuffer();
    return canvas->grab().toImage();
}

bool scanHit(QWidget *canvas, const QRect &area, const QString &id, QString &picked, QPoint &hit)
{
    // 只通过实际短点寻找可拾取像素，不复制生产相机/投影公式。
    for (int y = area.top() + 18; y < area.bottom() - 18; y += 9) {
        for (int x = area.left() + 12; x < area.right() - 12; x += 9) {
            picked = QStringLiteral("no-event");
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(x, y), 0);
            if (picked == id) { hit = QPoint(x, y); return true; }
        }
    }
    return false;
}

bool sceneInteraction(const QList<Part> &parts)
{
    SceneView scene;
    scene.resize(840, 560); scene.setParts(parts); scene.show(); QTest::qWait(160);
    auto *canvas = scene.findChild<QWidget *>(QStringLiteral("sceneCanvas"));
    REQUIRE(canvas && !scene.rendererDescription().contains(QStringLiteral("场景错误")));
    REQUIRE(!scene.dimensionsVisible());
    QString picked;
    int events = 0;
    scene.onPartPicked = [&](const QString &id) { picked = id; ++events; };
    scene.setSelectedPart(aId); scene.setHiddenParts({bId, cId});
    const QPoint blank(5, canvas->height() - 5);
    const int before = events;
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, blank);
    REQUIRE(events == before);
    doubleClick(canvas, blank);
    REQUIRE(events == before + 1 && picked.isEmpty());
    scene.setSelectedPart(aId);
    const int afterDouble = events;
    roundTripDrag(canvas, blank);
    REQUIRE(events == afterDouble);
    doubleSecondDrag(canvas, blank);
    REQUIRE(events == afterDouble);
    QTest::mouseDClick(canvas, Qt::RightButton, Qt::NoModifier, blank); drain();
    REQUIRE(events == afterDouble);
    const int beforeCancelled = events;
    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, blank);
    scene.setParts(parts);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, blank); drain();
    REQUIRE(events == beforeCancelled);
    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, blank);
    scene.setViewMode(SceneView::ViewMode::Front);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, blank); drain();
    REQUIRE(events == beforeCancelled);
    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, blank);
    // 制造平台失去鼠标抓取的真实取消事件，后续释放不能复活旧双击。
    QEvent cancel(QEvent::UngrabMouse); QApplication::sendEvent(canvas, &cancel);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, blank); drain();
    REQUIRE(events == beforeCancelled);
    scene.setSelectedPart(aId); scene.setHiddenParts({bId, cId});

    scene.setViewMode(SceneView::ViewMode::ThreeViews); drain();
    for (int column = 0; column < 3; ++column) {
        const int left = canvas->width() * column / 3;
        const int right = canvas->width() * (column + 1) / 3;
        const QRect pane(left, 0, right - left, canvas->height());
        QPoint hit;
        REQUIRE(scanHit(canvas, pane, aId, picked, hit));
        doubleClick(canvas, hit);
        REQUIRE(picked == aId);
        const int oldEvents = events;
        const QPoint paneBlank(left + 4, canvas->height() - 4);
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, paneBlank);
        REQUIRE(events == oldEvents);
        doubleClick(canvas, paneBlank);
        REQUIRE(picked.isEmpty() && events == oldEvents + 1);
    }
    // 隐藏对象不能被另一个三视图panel裁切失误拾取；返回全场景仍能选择精确尾空格ID。
    scene.setHiddenParts({});
    // 对齐Window“显示全部”的显式适配；setHiddenParts契约只显隐，不擅自改用户相机。
    scene.focusVisible(); drain();
    QPoint bHit;
    REQUIRE(scanHit(canvas, canvas->rect(), bId, picked, bHit));
    scene.setSelectedPart(bId); scene.setDimensionsVisible(true); drain();
    const auto labels = scene.measurementAnnotations();
    int dimensionCount = 0;
    for (const auto &label : labels) {
        if (label.axis.isEmpty()) continue;
        ++dimensionCount;
        REQUIRE(std::isfinite(label.millimeters) && label.millimeters > 0);
        // 实际业务读数而非控件存在性断言：尺寸不随投影/显示偏移变化。
        const long double expected = label.axis == QStringLiteral("X") ? 40 : label.axis == QStringLiteral("Y") ? 34 : 28;
        REQUIRE(std::abs(label.millimeters - expected) < 0.001L);
        REQUIRE(label.start.y() < canvas->height() / 3 || label.start.y() > canvas->height() * 2 / 3);
    }
    REQUIRE(dimensionCount == 3);
    REQUIRE(!pixels(canvas).isNull());
    if (!snapshots.isEmpty()) REQUIRE(pixels(canvas).save(QDir(snapshots).filePath(QStringLiteral("03-three-views-edge-dimensions.png"))));
    QTextStream(stdout) << "DESKTOP_SIM sceneMouse=1 threePanelPick=1 dragRoundTrip=1 dimensionsSource=1 renderer=" << scene.rendererDescription() << Qt::endl;
    return true;
}

bool failureRecovery(const QList<Part> &parts)
{
    SceneView scene; scene.resize(680, 450); scene.setParts(parts);
    scene.setSelectedPart(aId); REQUIRE(scene.setPullFraction(.4)); REQUIRE(scene.setExplosionFraction(.5));
    const auto aBefore = scene.displayOffset(aId); const auto bBefore = scene.displayOffset(bId);
    for (double invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -1.0, 2.0}) {
        REQUIRE(!scene.setPullFraction(invalid)); REQUIRE(!scene.setExplosionFraction(invalid));
        REQUIRE(scene.displayOffset(aId) == aBefore && scene.displayOffset(bId) == bBefore);
    }
    const QColor original = scene.partColor(aId);
    REQUIRE(!scene.setPartColor(aId, QColor()) && !scene.setPartColor(QStringLiteral("missing"), Qt::red));
    REQUIRE(scene.partColor(aId) == original);
    scene.setSelectedPart(QString()); REQUIRE(!scene.setPullFraction(.7));
    scene.setHiddenParts({aId}); scene.setSelectedPart(aId); REQUIRE(!scene.focusSelected());
    auto invalid = parts; invalid[0].positionX = std::numeric_limits<double>::infinity();
    scene.setParts(invalid); REQUIRE(scene.rendererDescription().contains(QStringLiteral("场景错误")));
    QList<Part> excessive;
    for (int index = 0; index <= 10000; ++index) excessive.append(parts.first());
    scene.setParts(excessive); REQUIRE(scene.rendererDescription().contains(QStringLiteral("场景错误")));
    excessive.clear();
    auto mesh = std::make_shared<MeshGeometry>();
    mesh->vertices = {{-1, -1, -1}, {1, -1, -1}, {0, 1, 1}};
    mesh->triangles = {{0, 1, 99}};
    Part corrupt = parts.first(); corrupt.shape = QStringLiteral("mesh"); corrupt.mesh = mesh;
    corrupt.sizeX = corrupt.sizeY = corrupt.sizeZ = 2;
    scene.setParts({corrupt}); REQUIRE(scene.rendererDescription().contains(QStringLiteral("场景错误")));
    // 分配受控约14MiB越界顶点表，Scene须在遍历/上传前拒绝，随后仍能恢复合法显示。
    mesh->vertices.resize(int(MeshGeometryCodec::maxVertices) + 1);
    scene.setParts({corrupt}); REQUIRE(scene.rendererDescription().contains(QStringLiteral("场景错误")));
    mesh->vertices.clear();
    scene.setParts(parts); REQUIRE(!scene.rendererDescription().contains(QStringLiteral("场景错误")));
    scene.setSelectedPart(aId); REQUIRE(scene.setPullFraction(.2));
    scene.restoreDisplayOffsets();
    for (const auto &part : parts) REQUIRE((scene.displayOffset(part.id) == std::array<long double, 3>{0, 0, 0}));
    return true;
}

bool windowAndPersistence(const QString &database, QList<Part> &source)
{
    EquipmentRepository repository; REQUIRE(repository.openDatabase(database) && repository.initialize());
    EquipmentController controller(repository); EquipmentWindow window(controller);
    REQUIRE(window.importData(fixture())); source = controller.parts();
    window.resize(1180, 760); window.show(); QTest::qWait(180);
    auto *list = window.findChild<QListWidget *>(QStringLiteral("partsList"));
    auto *isolate = window.findChild<QPushButton *>(QStringLiteral("isolateSelectedButton"));
    auto *all = window.findChild<QPushButton *>(QStringLiteral("showAllButton"));
    auto *scene = dynamic_cast<SceneView *>(window.findChild<QWidget *>(QStringLiteral("sceneView")));
    REQUIRE(list && isolate && all && scene);
    auto *canvas = scene->findChild<QWidget *>(QStringLiteral("sceneCanvas")); REQUIRE(canvas);
    auto *viewMode = window.findChild<QComboBox *>(QStringLiteral("viewModeSelector"));
    auto *projection = window.findChild<QComboBox *>(QStringLiteral("projectionSelector"));
    auto *axes = window.findChild<QComboBox *>(QStringLiteral("axisPlacementSelector"));
    auto *dims = window.findChild<QCheckBox *>(QStringLiteral("dimensionsCheck"));
    auto *grid = window.findChild<QCheckBox *>(QStringLiteral("gridCheck"));
    auto *explodeAxis = window.findChild<QComboBox *>(QStringLiteral("explodeAxisSelector"));
    auto *explosion = window.findChild<QSlider *>(QStringLiteral("explosionSlider"));
    auto *pull = window.findChild<QSlider *>(QStringLiteral("pullSlider"));
    auto *restore = window.findChild<QPushButton *>(QStringLiteral("restoreDisplayButton"));
    auto *restorePart = window.findChild<QPushButton *>(QStringLiteral("restorePartButton"));
    auto *colorButton = window.findChild<QPushButton *>(QStringLiteral("partColorButton"));
    auto *autoColor = window.findChild<QPushButton *>(QStringLiteral("autoColorButton"));
    REQUIRE(viewMode && projection && axes && dims && grid && explodeAxis && explosion && pull && restore && restorePart && colorButton && autoColor);
    REQUIRE(!dims->isChecked() && !scene->dimensionsVisible() && grid->isChecked() && scene->gridVisible());
    REQUIRE(choose(viewMode, 1) && scene->viewMode() == SceneView::ViewMode::Engineering);
    REQUIRE(choose(viewMode, 2));
    for (const auto mode : {SceneView::ViewMode::Front, SceneView::ViewMode::Top, SceneView::ViewMode::Right})
        REQUIRE(choose(projection, int(mode)) && scene->viewMode() == mode);
    REQUIRE(choose(viewMode, 3) && scene->viewMode() == SceneView::ViewMode::ThreeViews);
    REQUIRE(choose(viewMode, 0) && scene->viewMode() == SceneView::ViewMode::Isometric);
    for (const auto placement : {SceneView::AxisPlacement::Hidden, SceneView::AxisPlacement::Model,
        SceneView::AxisPlacement::Both, SceneView::AxisPlacement::Corner})
        REQUIRE(choose(axes, int(placement)) && scene->axisPlacement() == placement);
    // QCheckBox的可点击区域按indicator/文本算，布局拉宽后的控件中心未必命中。
    grid->setFocus(); QTest::keyClick(grid, Qt::Key_Space); REQUIRE(!scene->gridVisible());
    QTest::keyClick(grid, Qt::Key_Space); REQUIRE(scene->gridVisible());
    REQUIRE(snapshot(window, QStringLiteral("01-desktop-default.png")));
    REQUIRE(clickRow(list, aId)); QTest::mouseClick(isolate, Qt::LeftButton); drain();
    const QSet<QString> hiddenA{bId, cId}; const QSet<QString> hiddenB{aId, cId};
    REQUIRE(controller.isIsolationActive() && controller.hiddenPartIds() == hiddenA);
    dims->setFocus(); QTest::keyClick(dims, Qt::Key_Space); drain(); REQUIRE(scene->dimensionsVisible());
    REQUIRE(choose(explodeAxis, int(SceneView::ExplodeAxis::X)));
    explosion->setFocus(); QTest::keyClick(explosion, Qt::Key_End); drain(); REQUIRE(scene->explosionFraction() == 1);
    const auto aGlobal = scene->displayOffset(aId); const auto bGlobal = scene->displayOffset(bId); const auto cGlobal = scene->displayOffset(cId);
    pull->setFocus(); QTest::keyClick(pull, Qt::Key_End); drain(); REQUIRE(scene->pullFraction() == 1);
    REQUIRE(scene->displayOffset(aId) != aGlobal && scene->displayOffset(bId) == bGlobal && scene->displayOffset(cId) == cGlobal);
    QTest::mouseClick(restorePart, Qt::LeftButton); drain();
    REQUIRE(scene->pullFraction() == 0 && scene->displayOffset(aId) == aGlobal);
    QTest::mouseClick(restore, Qt::LeftButton); drain(); REQUIRE(scene->explosionFraction() == 0);
    const QColor oldColor = scene->partColor(aId);
    REQUIRE(colorChoice(colorButton, window, oldColor, false) && scene->partColor(aId) == oldColor);
    const QColor custom(QStringLiteral("#de21a4"));
    REQUIRE(colorChoice(colorButton, window, custom, true) && scene->partColor(aId) == custom);
    QTest::mouseClick(autoColor, Qt::LeftButton); REQUIRE(scene->partColor(aId) == oldColor);
    const QPoint blank(4, canvas->height() - 4);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, blank); drain();
    REQUIRE(controller.selectedPartId() == aId && controller.hiddenPartIds() == hiddenA);
    doubleClick(canvas, blank);
    REQUIRE(controller.selectedPartId().isEmpty() && controller.isIsolationActive() && controller.hiddenPartIds() == hiddenA);
    int isolatedDimensionCount = 0;
    for (const auto &label : scene->measurementAnnotations()) {
        if (label.axis.isEmpty()) continue;
        ++isolatedDimensionCount;
        const long double expected = label.axis == QStringLiteral("X") ? 40 : label.axis == QStringLiteral("Y") ? 34 : 28;
        REQUIRE(std::abs(label.millimeters - expected) < 0.001L);
    }
    REQUIRE(isolatedDimensionCount == 3);
    REQUIRE(snapshot(window, QStringLiteral("02-blank-double-clear-keeps-isolation.png")));
    REQUIRE(clickRow(list, bId)); REQUIRE(controller.selectedPartId() == bId && controller.hiddenPartIds() == hiddenB);
    REQUIRE(!window.loadEquipment(QStringLiteral("missing-device")));
    REQUIRE(!window.importData(QByteArray("{broken")));
    REQUIRE(!window.importData(fixture()));
    REQUIRE(controller.selectedPartId() == bId && controller.isIsolationActive() && controller.hiddenPartIds() == hiddenB);
    REQUIRE(sameSources(source, controller.parts()));
    scene->setSelectedPart(bId); REQUIRE(scene->setExplosionFraction(.8)); REQUIRE(scene->setPullFraction(.7));
    REQUIRE(scene->setPartColor(bId, QColor(QStringLiteral("#ff00aa"))));
    QTest::mouseClick(all, Qt::LeftButton); drain();
    REQUIRE(!controller.isIsolationActive() && controller.hiddenPartIds().isEmpty());
    QList<Part> persisted; REQUIRE(repository.loadParts(equipmentId, persisted)); REQUIRE(sameSources(source, persisted));
    EquipmentRepository reopened; REQUIRE(reopened.openDatabase(database) && reopened.initialize());
    QList<Part> afterRestart; REQUIRE(reopened.loadParts(equipmentId, afterRestart)); REQUIRE(sameSources(source, afterRestart));
    QTextStream(stdout) << "DESKTOP_SIM windowIsolation=1 sameNameExactId=1 failedLoadImportProtection=1 sourceDatabaseRestart=1" << Qt::endl;
    return true;
}

bool lifetimeCycles(const QList<Part> &parts)
{
    for (int cycle = 0; cycle < 8; ++cycle) {
        QList<QPointer<QObject>> borrowed;
        {
            SceneView scene; scene.resize(cycle % 2 ? 480 : 900, 520); scene.show();
            for (int load = 0; load < 4; ++load) {
                scene.setParts(parts); scene.setViewMode(SceneView::ViewMode::ThreeViews);
                scene.setSelectedPart(bId); scene.setExplosionFraction(.5); scene.setPullFraction(.8);
                scene.setDimensionsVisible(true); scene.setAxisPlacement(SceneView::AxisPlacement::Both);
                drain(); scene.setParts({}); drain();
            }
            for (auto *child : scene.findChildren<QObject *>()) borrowed.append(QPointer<QObject>(child));
        }
        drain();
        for (const auto &child : borrowed) REQUIRE(child.isNull());
    }
    QTextStream(stdout) << "DESKTOP_SIM resourceCycles=8 loadsPerCycle=4 ownedQObjectExpired=1 noZeroLeakClaim=1" << Qt::endl;
    return true;
}
}

int main(int argc, char **argv)
{
    // 色选器仍由真实Window按钮创建，使用Qt对话框使受控取消/确认不会占用系统原生模态窗口。
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const auto args = app.arguments();
    const int snapshotArg = args.indexOf(QStringLiteral("--snapshot-dir"));
    if (snapshotArg >= 0) {
        if (snapshotArg + 1 >= args.size()) return 1;
        snapshots = args[snapshotArg + 1];
        if (!QDir().mkpath(snapshots)) return 1;
    }
    QList<Part> source;
    if (!windowAndPersistence(temporary.filePath(QStringLiteral("independent.sqlite")), source)
        || !sceneInteraction(source) || !failureRecovery(source) || !lifetimeCycles(source)) return 1;
    QTextStream(stdout) << "DESKTOP_ASSEMBLY_SIM accepted=1 userAcceptance=0" << Qt::endl;
    return 0;
}
