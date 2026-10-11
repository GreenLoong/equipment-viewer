#include "equipmentcontroller.h"
#include "equipmentwindow.h"
#include "sceneview.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QVector3D>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

// R11 独立验收：只通过公开窗口/控件与真实鼠标像素验证用户反馈的四个问题，
// 不复制生产实现的相机、投影或网格公式，也不接触正常数据库与后端服务。
namespace {
#define REQUIRE(condition) do { if (!(condition)) { \
    QTextStream(stderr) << "R11_TEST_FAIL " << __LINE__ << ": " << #condition << Qt::endl; \
    return false; } } while (false)

const QString equipmentId = QStringLiteral("R11-ACCEPT");
QString snapshots;

QByteArray fixture()
{
    QJsonArray parts;
    const QStringList ids{QStringLiteral("PLATE"), QStringLiteral("SHAFT"), QStringLiteral("BLOCK")};
    const QList<double> positions{-60, 0, 70};
    for (int index = 0; index < ids.size(); ++index) {
        parts.append(QJsonObject{{"id", ids[index]}, {"name", ids[index] + QStringLiteral(" 件")},
            {"material", QStringLiteral("R11材料")}, {"shape", "box"},
            {"dimensions", QJsonArray{40, 30, 20}}, {"position", QJsonArray{positions[index], 0, 40}}});
    }
    return QJsonDocument(QJsonObject{{"equipmentId", equipmentId},
        {"equipmentName", QStringLiteral("R11 交互验收件")}, {"parts", parts}}).toJson(QJsonDocument::Compact);
}

void drain()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QImage frame(QWidget *canvas)
{
    if (auto *gl = qobject_cast<QOpenGLWidget *>(canvas)) return gl->grabFramebuffer();
    return canvas ? canvas->grab().toImage() : QImage();
}

bool saveShot(QWidget &window, const QString &name)
{
    if (snapshots.isEmpty()) return true;
    drain();
    if (QApplication::platformName() != QStringLiteral("windows")) return true;
    window.raise(); window.activateWindow(); QTest::qWait(80);
    QScreen *screen = window.screen();
    if (!screen) return false;
    const QRect client(QRect(window.mapToGlobal(QPoint()), window.size()));
    const QPoint origin = client.topLeft() - screen->geometry().topLeft();
    const QImage image = screen->grabWindow(0, origin.x(), origin.y(), client.width(), client.height()).toImage();
    return !image.isNull() && image.save(QDir(snapshots).filePath(name));
}

// 拖动由真实鼠标事件产生；右键/中键拖动不进入短点选择路径。
void drag(QWidget *canvas, Qt::MouseButton button, const QPoint &delta)
{
    const QPoint start(canvas->width() / 2, canvas->height() / 2);
    QTest::mousePress(canvas, button, Qt::NoModifier, start);
    const QPoint end = start + delta;
    QMouseEvent move(QEvent::MouseMove, QPointF(end), QPointF(canvas->mapToGlobal(end)),
        Qt::NoButton, button, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
    QTest::mouseRelease(canvas, button, Qt::NoModifier, end);
    drain();
}

void wheel(QWidget *canvas, int steps)
{
    for (int index = 0; index < steps; ++index) {
        const QPointF center(canvas->width() / 2.0, canvas->height() / 2.0);
        QWheelEvent event(center, center, QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
            Qt::NoScrollPhase, false);
        QApplication::sendEvent(canvas, &event);
    }
    drain();
}

void doubleClickBlank(QWidget *canvas)
{
    const QPoint blank(4, 4);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, blank);
    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, blank);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, blank);
    drain();
}

bool clickRow(QListWidget *list, const QString &id)
{
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->data(Qt::UserRole).toString() != id) continue;
        list->scrollToItem(list->item(row));
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
            list->visualItemRect(list->item(row)).center());
        drain();
        return true;
    }
    return false;
}

bool nearColor(const QColor &a, const QColor &b, int tolerance = 46)
{
    return std::abs(a.red() - b.red()) <= tolerance
        && std::abs(a.green() - b.green()) <= tolerance
        && std::abs(a.blue() - b.blue()) <= tolerance;
}

// 鼠标约定：左键旋转、右键平移、中键缩放，右键/中键都不得改变选择。
bool mouseContract(SceneView &scene, QWidget *canvas, const QString &selected)
{
    scene.setSelectedPart(selected);
    const QVector3D start = scene.cameraTarget();
    const QImage startFrame = frame(canvas);
    drag(canvas, Qt::RightButton, QPoint(80, 26));
    REQUIRE(scene.cameraTarget() != start);
    REQUIRE(frame(canvas) != startFrame);
    const QVector3D panned = scene.cameraTarget();
    drag(canvas, Qt::LeftButton, QPoint(70, 18));
    REQUIRE(scene.cameraTarget() == panned); // 左键旋转不改观察点
    REQUIRE(frame(canvas) != startFrame);
    const QVector3D beforeDolly = scene.cameraTarget();
    const QImage beforeDollyFrame = frame(canvas);
    drag(canvas, Qt::MiddleButton, QPoint(0, 70));
    REQUIRE(scene.cameraTarget() == beforeDolly); // 中键缩放不改观察点
    REQUIRE(frame(canvas) != beforeDollyFrame);
    scene.setViewMode(SceneView::ViewMode::Front);
    const QVector3D frontStart = scene.cameraTarget();
    drag(canvas, Qt::RightButton, QPoint(56, 0));
    REQUIRE(scene.cameraTarget() != frontStart); // 正交视图也要能平移
    scene.setViewMode(SceneView::ViewMode::Isometric);
    scene.fitAll();
    REQUIRE(scene.cameraTarget() == QVector3D()); // 视角回正复位平移
    QTextStream(stdout) << "R11_TESTER mouse leftRotate=1 rightPan=1 middleZoom=1 orthoPan=1 fitResetsPan=1" << Qt::endl;
    return true;
}

// 抗锯齿：报告的采样/超采样必须真实生效，且轮廓上要有过渡像素而不是硬台阶。
bool antiAliasing(SceneView &scene, QWidget *canvas)
{
    const QString renderer = scene.rendererDescription();
    QTextStream(stdout) << "R11_TESTER renderer='" << renderer << "'" << Qt::endl;
    REQUIRE(!renderer.contains(QStringLiteral("场景错误")));
    if (renderer.contains(QStringLiteral("软件"))) {
        QTextStream(stdout) << "R11_TESTER antialiasing skipped on software fallback" << Qt::endl;
        return true;
    }
    REQUIRE(renderer.contains(QStringLiteral("绘制FBO实际samples=")));
    const int samplesIndex = renderer.indexOf(QStringLiteral("绘制FBO实际samples="));
    const QString samplesText = renderer.mid(samplesIndex + QStringLiteral("绘制FBO实际samples=").length(), 1);
    bool ok = false;
    const int samples = samplesText.toInt(&ok);
    REQUIRE(ok);
    const int supersampleIndex = renderer.indexOf(QStringLiteral("超采样"));
    REQUIRE(supersampleIndex > 0);
    const QString supersampleText = renderer.mid(supersampleIndex + QStringLiteral("超采样").length(), 3);
    const double supersample = supersampleText.left(3).toDouble(&ok);
    REQUIRE(ok && supersample >= 1.0);
    REQUIRE(samples > 0 || supersample > 1.0); // 至少一条真实生效的抗锯齿路径
    // 轮廓过渡像素：水平扫描一条穿过模型的线，必须存在既非背景也非面色的中间色像素。
    const QImage image = frame(canvas);
    REQUIRE(!image.isNull());
    const QColor background = image.pixelColor(2, 2);
    int transitions = 0;
    const int row = image.height() / 2;
    for (int x = 1; x < image.width() - 1; ++x) {
        const QColor left = image.pixelColor(x - 1, row);
        const QColor middle = image.pixelColor(x, row);
        const QColor right = image.pixelColor(x + 1, row);
        const bool leftBackground = nearColor(left, background, 10);
        const bool rightBackground = nearColor(right, background, 10);
        if (leftBackground == rightBackground) continue;
        const QColor inside = leftBackground ? right : left;
        const bool between = middle.red() > std::min(background.red(), inside.red()) + 4
            && middle.red() < std::max(background.red(), inside.red()) - 4;
        if (between && !nearColor(middle, background, 6) && !nearColor(middle, inside, 6)) ++transitions;
    }
    REQUIRE(transitions > 0);
    QTextStream(stdout) << "R11_TESTER antialiasing samples=" << samples << " supersample=" << supersample
                        << " smoothTransitions=" << transitions << " renderer='" << renderer << "'" << Qt::endl;
    return true;
}

// 右下角坐标轴：三色轴都要出现，整体落在视口内，并且不压到左下比例尺。
bool cornerTriad(SceneView &scene, QWidget *canvas)
{
    scene.setAxisPlacement(SceneView::AxisPlacement::Corner);
    scene.fitAll();
    drain();
    const QImage image = frame(canvas);
    REQUIRE(!image.isNull());
    const QColor axisColors[3] = {QColor(233, 118, 113), QColor(139, 208, 154), QColor(111, 181, 239)};
    int left = image.width(), top = image.height(), right = -1, bottom = -1, found = 0;
    for (int axis = 0; axis < 3; ++axis) {
        bool axisFound = false;
        for (int y = image.height() / 3; y < image.height(); ++y) {
            for (int x = image.width() * 2 / 3; x < image.width(); ++x) {
                if (!nearColor(image.pixelColor(x, y), axisColors[axis], 40)) continue;
                axisFound = true;
                left = std::min(left, x); top = std::min(top, y);
                right = std::max(right, x); bottom = std::max(bottom, y);
            }
        }
        if (axisFound) ++found;
    }
    REQUIRE(found == 3);
    REQUIRE(left >= 0 && top >= 0 && right < image.width() && bottom < image.height());
    REQUIRE(left > image.width() / 2 && top > image.height() / 2); // 落在右下角，不跑到中间
    REQUIRE(right - left < image.width() / 3 && bottom - top < image.height() / 3); // 尺寸收敛，不是散乱长线
    QTextStream(stdout) << "R11_TESTER triad axes=3 bbox=" << left << ',' << top << ',' << right << ',' << bottom
                        << " canvas=" << image.width() << 'x' << image.height() << Qt::endl;
    return true;
}

// 参考网格：范围受限，且深缩放后视口左上角仍是纯背景，不再出现放射状长线。
// 四种视图模式都必须留出背景：整屏被实体铺满说明正交/三视图取景或视口换算坏了。
bool viewModesKeepBackground(SceneView &scene, QWidget *canvas)
{
    const std::array<SceneView::ViewMode, 4> modes = {SceneView::ViewMode::Engineering,
        SceneView::ViewMode::Front, SceneView::ViewMode::Top, SceneView::ViewMode::ThreeViews};
    const QStringList names{QStringLiteral("engineering"), QStringLiteral("front"),
        QStringLiteral("top"), QStringLiteral("threeViews")};
    scene.setAxisPlacement(SceneView::AxisPlacement::Hidden);
    scene.setDimensionsVisible(false);
    for (int index = 0; index < int(modes.size()); ++index) {
        scene.setViewMode(modes[index]);
        drain();
        const QImage image = frame(canvas);
        REQUIRE(!image.isNull());
        // 背景色由产品定义：工程轴测为近白，其余为深色；实体像素占比必须留出明显背景。
        const QColor background = image.pixelColor(3, 3);
        int same = 0, total = 0;
        for (int y = 2; y < image.height() - 2; y += 4)
            for (int x = 2; x < image.width() - 2; x += 4) {
                ++total;
                if (nearColor(image.pixelColor(x, y), background, 12)) ++same;
            }
        REQUIRE(total > 0);
        const double backgroundRatio = double(same) / total;
        REQUIRE(backgroundRatio > 0.15); // 至少15%是背景，避免整屏被实体铺满
        if (!snapshots.isEmpty()) image.save(QDir(snapshots).filePath(QStringLiteral("05-r11-%1.png").arg(names[index])));
        QTextStream(stdout) << "R11_TESTER viewMode=" << names[index] << " backgroundRatio=" << backgroundRatio
                            << " cornerColor=" << background.name() << Qt::endl;
    }
    scene.setViewMode(SceneView::ViewMode::Isometric);
    return true;
}

bool boundedGrid(SceneView &scene, QWidget *canvas)
{
    scene.setGridVisible(true);
    scene.setAxisPlacement(SceneView::AxisPlacement::Hidden);
    scene.setViewMode(SceneView::ViewMode::Isometric);
    scene.fitAll();
    drain();
    REQUIRE(scene.gridLineCount() > 0 && scene.gridLineCount() <= 60);
    const auto baseExtent = scene.gridExtentMillimeters();
    REQUIRE(baseExtent[0] > 0 && baseExtent[0] == baseExtent[1]);
    wheel(canvas, 10);
    REQUIRE(scene.gridLineCount() > 0 && scene.gridLineCount() <= 60);
    const auto zoomExtent = scene.gridExtentMillimeters();
    REQUIRE(zoomExtent[0] > 0);
    REQUIRE(zoomExtent[0] <= baseExtent[0] * 1.5L); // 放大不会让网格范围发散
    const QImage image = frame(canvas);
    REQUIRE(!image.isNull());
    const QColor background = image.pixelColor(1, 1);
    int foreign = 0;
    for (int y = 2; y < std::min(40, image.height()); ++y)
        for (int x = 2; x < std::min(40, image.width()); ++x)
            if (!nearColor(image.pixelColor(x, y), background, 12)) ++foreign;
    REQUIRE(foreign == 0); // 旧版无限网格会在这里留下放射状长线
    QTextStream(stdout) << "R11_TESTER grid lines=" << scene.gridLineCount()
                        << " baseHalf=" << double(baseExtent[0]) << "mm zoomHalf=" << double(zoomExtent[0])
                        << "mm cornerForeignPixels=" << foreign << Qt::endl;
    scene.fitAll();
    return true;
}

bool windowFlow(const QString &database, QStringList &fingerprint)
{
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(database) && repository.initialize());
    EquipmentController controller(repository);
    EquipmentWindow window(controller);
    REQUIRE(window.importData(fixture()));
    window.resize(1280, 800);
    window.show();
    QTest::qWait(200);
    auto *list = window.findChild<QListWidget *>(QStringLiteral("partsList"));
    auto *isolate = window.findChild<QPushButton *>(QStringLiteral("isolateSelectedButton"));
    auto *showAll = window.findChild<QPushButton *>(QStringLiteral("showAllButton"));
    REQUIRE(list && isolate && showAll);
    auto *scene = dynamic_cast<SceneView *>(window.findChild<QWidget *>(QStringLiteral("sceneView")));
    REQUIRE(scene);
    auto *canvas = scene->findChild<QWidget *>(QStringLiteral("sceneCanvas"));
    REQUIRE(canvas);
    REQUIRE(saveShot(window, QStringLiteral("01-r11-default.png")));

    const QString first = QStringLiteral("PLATE");
    REQUIRE(clickRow(list, first));
    QTest::mouseClick(isolate, Qt::LeftButton);
    drain();
    REQUIRE(controller.isIsolationActive());
    REQUIRE(mouseContract(*scene, canvas, first));
    // 隔离中右键拖动不应改变选择，双击空白只清选中并保持隔离。
    REQUIRE(controller.selectedPartId() == first);
    doubleClickBlank(canvas);
    REQUIRE(controller.selectedPartId().isEmpty() && controller.isIsolationActive());
    REQUIRE(controller.isolatedPartId() == first);
    REQUIRE(saveShot(window, QStringLiteral("02-r11-pan-cleared-selection.png")));
    QTest::mouseClick(showAll, Qt::LeftButton);
    drain();
    REQUIRE(!controller.isIsolationActive());
    REQUIRE(antiAliasing(*scene, canvas));
    REQUIRE(cornerTriad(*scene, canvas));
    REQUIRE(saveShot(window, QStringLiteral("03-r11-corner-triad.png")));
    REQUIRE(boundedGrid(*scene, canvas));
    REQUIRE(saveShot(window, QStringLiteral("04-r11-bounded-grid.png")));
    REQUIRE(viewModesKeepBackground(*scene, canvas));
    // 交互只改显示状态，源参数必须与数据库逐字段一致。
    for (const Part &part : controller.parts())
        fingerprint.append(QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
            .arg(part.id).arg(part.name)
            .arg(part.positionX, 0, 'g', 17).arg(part.positionY, 0, 'g', 17)
            .arg(part.positionZ, 0, 'g', 17)
            .arg(part.sizeX.value_or(0), 0, 'g', 17).arg(part.sizeY.value_or(0), 0, 'g', 17));
    return true;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const auto arguments = app.arguments();
    const int snapshotArg = arguments.indexOf(QStringLiteral("--snapshot-dir"));
    if (snapshotArg >= 0) {
        if (snapshotArg + 1 >= arguments.size()) return 1;
        snapshots = arguments[snapshotArg + 1];
        if (!QDir().mkpath(snapshots)) return 1;
    }
    const QString database = temporary.filePath(QStringLiteral("r11.sqlite"));
    QStringList fingerprint;
    if (!windowFlow(database, fingerprint)) return 1;
    // 重新打开同一个临时库，核对显示操作没有写回任何源参数。
    EquipmentRepository reopened;
    QStringList afterRestart;
    if (!reopened.openDatabase(database) || !reopened.initialize()) return 1;
    QList<Part> parts;
    if (!reopened.loadParts(equipmentId, parts)) return 1;
    for (const Part &part : parts)
        afterRestart.append(QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
            .arg(part.id).arg(part.name)
            .arg(part.positionX, 0, 'g', 17).arg(part.positionY, 0, 'g', 17)
            .arg(part.positionZ, 0, 'g', 17)
            .arg(part.sizeX.value_or(0), 0, 'g', 17).arg(part.sizeY.value_or(0), 0, 'g', 17));
    if (afterRestart != fingerprint) {
        QTextStream(stderr) << "R11_TEST_FAIL database fingerprint changed" << Qt::endl;
        return 1;
    }
    QTextStream(stdout) << "R11_TESTER accepted=1 userAcceptance=0 platform="
                        << QApplication::platformName() << Qt::endl;
    return 0;
}
