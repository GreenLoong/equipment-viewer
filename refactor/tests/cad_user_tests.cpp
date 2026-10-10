#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include "cadimportprocess.h"
#include "equipmentcontroller.h"
#include "equipmentwindow.h"
#include "sceneview.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QClipboard>
#include <QColor>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPair>
#include <QPixmap>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QSlider>
#include <QHash>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

// R08来自真人需求；本文件为Agent真实控件试用/自动验收，不冒充真人再次验收。
namespace {
QString bearingPath;
QString assemblyPath;
QString rejectedPath;
QString snapshots;
int expectedAssembly = 0;
int resourceCycles = 0;
std::array<double, 3> expectedBearing{22, 22, 7};

#define REQUIRE(condition) do { if (!(condition)) { \
    QTextStream(stderr) << "CAD USER FAIL " << __LINE__ << ": " << #condition << Qt::endl; \
    return false; } } while (false)

bool waitUntil(const std::function<bool()> &condition, int timeoutMs = 130000)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents();
        if (condition()) return true;
        QTest::qWait(10);
    } while (timer.elapsed() < timeoutMs);
    return false;
}

void drain()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QString sourceHash(const QString &filename)
{
    QFile input(filename);
    if (!input.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!input.atEnd()) hash.addData(input.read(65536));
    return input.error() == QFileDevice::NoError ? QString::fromLatin1(hash.result().toHex()) : QString();
}

CadImportProcess *worker(EquipmentWindow &window)
{
    for (auto *child : window.findChildren<QObject *>())
        if (auto *cad = dynamic_cast<CadImportProcess *>(child)) return cad;
    return nullptr;
}

QImage scenePixels(SceneView &scene)
{
    auto *canvas = scene.findChild<QWidget *>(QStringLiteral("sceneCanvas"));
    if (auto *native = dynamic_cast<QOpenGLWidget *>(canvas)) return native->grabFramebuffer();
    return canvas ? canvas->grab().toImage() : QImage();
}

bool capture(QWidget &window, const QString &name)
{
    if (snapshots.isEmpty()) return true;
    QCoreApplication::processEvents();
    if (QApplication::platformName() == QStringLiteral("windows")) {
        QScreen *screen = QApplication::primaryScreen();
        REQUIRE(screen);
        const QRect available = screen->availableGeometry();
        REQUIRE(available.width() > 400 && available.height() > 300);
        window.resize(std::min(1320, available.width() - 40), std::min(820, available.height() - 60));
        window.move(available.topLeft() + QPoint(16, 16));
        window.raise(); window.activateWindow();
        QRect frame, client, previousFrame, previousClient;
        QSize actualSize, minimumSize, previousSize, previousMinimum;
        QElapsedTimer unchanged; unchanged.start();
        // native resize/layout/paint可能排队；只等待真实几何收敛，不改minimum或放松屏幕包含检查。
        const bool settled = waitUntil([&] {
            frame = window.frameGeometry(); client = QRect(window.mapToGlobal(QPoint()), window.size());
            actualSize = window.size(); minimumSize = window.minimumSize();
            if (frame != previousFrame || client != previousClient || actualSize != previousSize || minimumSize != previousMinimum) {
                previousFrame = frame; previousClient = client; previousSize = actualSize; previousMinimum = minimumSize;
                unchanged.restart();
            }
            return available.contains(frame) && available.contains(client) && unchanged.elapsed() >= 150;
        }, 3000);
        QTextStream(stdout) << "CAD_CAPTURE available=" << available.x() << ',' << available.y() << ',' << available.width() << ',' << available.height()
            << " frame=" << frame.x() << ',' << frame.y() << ',' << frame.width() << ',' << frame.height()
            << " client=" << client.x() << ',' << client.y() << ',' << client.width() << ',' << client.height()
            << " size=" << actualSize.width() << ',' << actualSize.height()
            << " minimumSize=" << minimumSize.width() << ',' << minimumSize.height() << " settled=" << settled
            << " dpr=" << screen->devicePixelRatio() << Qt::endl;
        REQUIRE(available.contains(frame)); // 不能通过改产品minimum/layout掩盖无法完整显示的窗口。
        REQUIRE(available.contains(client));
        REQUIRE(settled);
        const QPoint screenPoint = client.topLeft() - screen->geometry().topLeft();
        // 取实际桌面上本窗口客户区，避免Windows高DPI的winId抓图裁切/黑白填充。
        const auto pixels = screen->grabWindow(0, screenPoint.x(), screenPoint.y(), client.width(), client.height());
        if (auto *scene = dynamic_cast<SceneView *>(window.findChild<QWidget *>(QStringLiteral("sceneView")))) {
            auto *canvas = scene->findChild<QWidget *>(QStringLiteral("sceneCanvas"));
            REQUIRE(canvas && !pixels.isNull());
            const QImage image = pixels.toImage();
            const auto mode = scene->viewMode();
            const double ratio = double(image.width()) / window.width();
            const QRect roi(QPointF(canvas->mapTo(&window, QPoint()) * ratio).toPoint(),
                            QSize(int(canvas->width() * ratio), int(canvas->height() * ratio)));
            int background = 0, samples = 0;
            for (int y = roi.top(); y <= roi.bottom(); y += 8) for (int x = roi.left(); x <= roi.right(); x += 8) {
                if (!image.rect().contains(x, y)) continue;
                const QColor color = image.pixelColor(x, y); ++samples;
                const bool light = color.red() > 235 && color.green() > 235 && color.blue() > 240;
                const bool dark = color.red() < 24 && color.green() < 30 && color.blue() < 39;
                const bool colored = color.red() >= 20 && color.red() <= 45 && color.green() >= 25 && color.green() <= 55
                    && color.blue() >= 40 && color.blue() <= 70;
                if (mode == SceneView::ViewMode::Engineering ? light : mode == SceneView::ViewMode::Isometric ? colored : dark) ++background;
            }
            pixels.save(QDir(snapshots).filePath(name)); // 失败也留原截图供定位，不合成伪实屏。
            REQUIRE(samples > 0 && background * 10 > samples);
        }
        return !pixels.isNull() && pixels.save(QDir(snapshots).filePath(name));
    }
    return window.grab().save(QDir(snapshots).filePath(name));
}

bool clickPart(EquipmentWindow &window, const QString &id, bool twice = false)
{
    auto *list = window.findChild<QListWidget *>(QStringLiteral("partsList"));
    REQUIRE(list);
    auto point = [&]() -> QPoint {
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->data(Qt::UserRole).toString() == id) {
                list->scrollToItem(list->item(row));
                return list->visualItemRect(list->item(row)).center();
            }
        }
        return {-1, -1};
    };
    QPoint target = point();
    REQUIRE(target.x() >= 0);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, target);
    drain();
    if (twice) {
        target = point(); // 双击前重取item坐标；QTimer重建后不能持有旧item。
        REQUIRE(target.x() >= 0);
        QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, target);
        QTest::qWait(30); drain();
    }
    return true;
}

bool pasteSearch(QLineEdit *search, const QString &text)
{
    REQUIRE(search && search->isEnabled());
    auto previous = std::make_unique<QMimeData>();
    const auto *mime = QApplication::clipboard()->mimeData();
    if (mime) for (const auto &format : mime->formats()) previous->setData(format, mime->data(format));
    QApplication::clipboard()->setText(text);
    QTest::mouseClick(search, Qt::LeftButton);
    QTest::keyClick(search, Qt::Key_A, Qt::ControlModifier);
    if (text.isEmpty()) QTest::keyClick(search, Qt::Key_Backspace);
    else QTest::keyClick(search, Qt::Key_V, Qt::ControlModifier);
    drain();
    QApplication::clipboard()->setMimeData(previous.release());
    REQUIRE(search->text() == text);
    return true;
}

void drag(QWidget *canvas, QPoint delta)
{
    const QPoint start = canvas->rect().center();
    const QPoint end = start + delta;
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
    QMouseEvent move(QEvent::MouseMove, QPointF(end), QPointF(canvas->mapToGlobal(end)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
    QTest::qWait(60);
}

bool importAndWait(EquipmentWindow &window, const QString &path)
{
    bool eventTick = false;
    REQUIRE(window.importFile(path)); // 与文件选择按钮共用公开入口；path由用户/参数输入。
    REQUIRE(window.cadImportInProgress());
    QTimer::singleShot(0, &window, [&eventTick] { eventTick = true; });
    REQUIRE(waitUntil([&] { return !window.cadImportInProgress(); }));
    auto *cad = worker(window);
    REQUIRE(eventTick && cad && !cad->isBusy() && cad->processId() == 0);
    return true;
}

bool meshFields(const Part &part, const QString &path)
{
    // CAD装配可能合法含简单盒状/楔形实体，不强迫每件都有曲面；轴承另验真孔。
    REQUIRE(part.shape == QStringLiteral("mesh") && part.mesh && part.mesh->vertices.size() >= 3 && !part.mesh->triangles.isEmpty());
    REQUIRE(part.cadSourceName == QFileInfo(path).fileName() && part.cadSourceSha256 == sourceHash(path));
    REQUIRE(part.sizeX && part.sizeY && part.sizeZ && *part.sizeX > 0 && *part.sizeY > 0 && *part.sizeZ > 0);
    REQUIRE(!part.radius && !part.length && !part.name.trimmed().isEmpty() && !part.material.trimmed().isEmpty());
    const std::array<double, 3> dimensions{*part.sizeX, *part.sizeY, *part.sizeZ};
    const std::array<double, 3> position{part.positionX, part.positionY, part.positionZ};
    std::array<double, 3> minimum, maximum;
    minimum.fill(std::numeric_limits<double>::infinity());
    maximum.fill(-std::numeric_limits<double>::infinity());
    for (const auto &vertex : part.mesh->vertices) for (int axis = 0; axis < 3; ++axis) {
        REQUIRE(std::isfinite(vertex[axis]) && std::isfinite(position[axis] + vertex[axis]));
        REQUIRE(std::abs(vertex[axis]) <= dimensions[axis] / 2 + 0.001);
        minimum[axis] = std::min(minimum[axis], vertex[axis]);
        maximum[axis] = std::max(maximum[axis], vertex[axis]);
    }
    for (int axis = 0; axis < 3; ++axis) {
        REQUIRE(std::isfinite(position[axis]) && dimensions[axis] > 0);
        REQUIRE(maximum[axis] - minimum[axis] >= dimensions[axis] * 0.95);
    }
    for (const auto &triangle : part.mesh->triangles)
        for (const auto index : triangle) REQUIRE(index < quint32(part.mesh->vertices.size()));
    return true;
}

QStringList fingerprints(const QList<Part> &parts)
{
    QStringList result;
    for (const auto &part : parts) {
        QByteArray bytes;
        QString error;
        if (!part.mesh || !MeshGeometryCodec::encode(*part.mesh, bytes, error)) return {};
        result << QStringList{part.id, part.cadSourceSha256,
            QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()),
            QString::number(part.positionX, 'g', 17), QString::number(part.positionY, 'g', 17), QString::number(part.positionZ, 'g', 17),
            QString::number(part.sizeX.value_or(0), 'g', 17), QString::number(part.sizeY.value_or(0), 'g', 17),
            QString::number(part.sizeZ.value_or(0), 'g', 17), part.cadSourceName, part.name, part.material, part.shape}.join(QLatin1Char('|'));
    }
    result.sort();
    return result;
}

using Triple = std::array<double, 3>;
Triple subtract(const Triple &a, const Triple &b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Triple cross(const Triple &a, const Triple &b) { return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]}; }
double dot(const Triple &a, const Triple &b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
int rayHits(const MeshGeometry &mesh, const Triple &origin, const Triple &direction)
{
    int hits = 0;
    for (const auto &indices : mesh.triangles) {
        const auto &a = mesh.vertices[indices[0]], &b = mesh.vertices[indices[1]], &c = mesh.vertices[indices[2]];
        const auto edge1 = subtract(b, a), edge2 = subtract(c, a), h = cross(direction, edge2);
        const double determinant = dot(edge1, h);
        if (std::abs(determinant) < 1e-10) continue;
        const double inverse = 1 / determinant;
        const auto s = subtract(origin, a);
        const double u = inverse * dot(s, h);
        const auto q = cross(s, edge1);
        const double v = inverse * dot(direction, q);
        if (u >= -1e-7 && v >= -1e-7 && u + v <= 1 + 1e-7 && inverse * dot(edge2, q) > 0) ++hits;
    }
    return hits;
}

bool bearingHole(const Part &part)
{
    REQUIRE(part.mesh->vertices.size() > 12 && part.mesh->triangles.size() > 12);
    const Triple dimensions{*part.sizeX, *part.sizeY, *part.sizeZ};
    int thin = int(std::min_element(dimensions.begin(), dimensions.end()) - dimensions.begin());
    Triple origin{}, direction{};
    origin[thin] = -dimensions[thin] - 10;
    direction[thin] = 1;
    REQUIRE(rayHits(*part.mesh, origin, direction) == 0);
    const int radial = (thin + 1) % 3;
    origin[radial] = dimensions[radial] * 0.35;
    REQUIRE(rayHits(*part.mesh, origin, direction) > 0); // 真孔+外环，包围盒代理会击中中心。
    return true;
}

bool blue(const QColor &color) { return color.blue() > color.red()*1.6 && color.green() > color.red()*1.3 && color.blue() > 70; }
bool orange(const QColor &color) { return color.red() > 100 && color.red() > color.green()*1.2 && color.green() > color.blue()*2; }

bool displayedPart(EquipmentWindow &window, SceneView &scene, const Part &part)
{
    REQUIRE(window.findChild<QLabel *>(QStringLiteral("partId"))->text() == part.id);
    REQUIRE(window.findChild<QLabel *>(QStringLiteral("shape"))->text().contains(QStringLiteral("CAD")));
    auto *source = window.findChild<QLabel *>(QStringLiteral("cadSource"));
    REQUIRE(source && source->isVisible() && source->text().contains(part.cadSourceName) && source->toolTip() == part.cadSourceSha256);
    REQUIRE(window.findChild<QGroupBox *>(QStringLiteral("boxDimensions"))->title().contains(QStringLiteral("外包络")));
    REQUIRE(window.findChild<QGroupBox *>(QStringLiteral("positionGroup"))->title().contains(QStringLiteral("外包络中心")));
    const Triple dimensions{*part.sizeX, *part.sizeY, *part.sizeZ};
    const QStringList labels{QStringLiteral("sizeX"), QStringLiteral("sizeY"), QStringLiteral("sizeZ")};
    for (int axis = 0; axis < 3; ++axis) {
        REQUIRE(std::abs(window.findChild<QLabel *>(labels[axis])->text().toDouble() - dimensions[axis]) < 0.001);
    }
    int axes = 0;
    auto *dimensionsControl = window.findChild<QCheckBox *>(QStringLiteral("dimensionsCheck"));
    REQUIRE(dimensionsControl);
    if (!dimensionsControl->isChecked()) { dimensionsControl->setFocus(); QTest::keyClick(dimensionsControl, Qt::Key_Space); }
    REQUIRE(dimensionsControl->isChecked() && scene.dimensionsVisible());
    for (const auto &measurement : scene.measurementAnnotations()) {
        const int axis = QStringLiteral("XYZ").indexOf(measurement.axis);
        if (measurement.axis.size() == 1 && axis >= 0) {
            REQUIRE(std::abs(double(measurement.millimeters) - dimensions[axis]) < 0.001);
            ++axes;
        }
    }
    REQUIRE(axes == 3);
    return true;
}

bool failedImportPreserves(EquipmentWindow &window, EquipmentController &controller, const QString &path)
{
    const QString before = controller.equipment()->id;
    const auto beforeMesh = fingerprints(controller.parts());
    const auto hidden = controller.hiddenPartIds();
    const QString selection = controller.selectedPartId();
    QList<Equipment> devicesBefore;
    REQUIRE(controller.availableEquipment(devicesBefore));
    REQUIRE(importAndWait(window, path));
    REQUIRE(controller.equipment()->id == before && fingerprints(controller.parts()) == beforeMesh
            && controller.hiddenPartIds() == hidden && controller.selectedPartId() == selection);
    const QString message = window.findChild<QLabel *>(QStringLiteral("equipmentStatus"))->text();
    REQUIRE(!message.contains(QStringLiteral("已导入并保存"))
            && (message.contains(QStringLiteral("失败")) || message.contains(QStringLiteral("存在"))));
    QList<Equipment> devicesAfter;
    REQUIRE(controller.availableEquipment(devicesAfter) && devicesBefore.size() == devicesAfter.size());
    for (int index = 0; index < devicesBefore.size(); ++index)
        REQUIRE(devicesBefore[index].id == devicesAfter[index].id && devicesBefore[index].name == devicesAfter[index].name);
    return true;
}

bool chooseCombo(QComboBox *combo, int data)
{
    REQUIRE(combo && combo->isVisible() && combo->isEnabled());
    const int index = combo->findData(data);
    REQUIRE(index >= 0);
    combo->setFocus(); QTest::keyClick(combo, Qt::Key_Home);
    for (int step = 0; step < index; ++step) QTest::keyClick(combo, Qt::Key_Down);
    drain(); QTest::qWait(50);
    REQUIRE(combo->currentData().toInt() == data);
    return true;
}

bool sliderMaximum(QSlider *slider)
{
    REQUIRE(slider && slider->isVisible() && slider->isEnabled());
    slider->setFocus(); QTest::keyClick(slider, Qt::Key_End); drain(); QTest::qWait(60);
    REQUIRE(slider->value() == 100);
    return true;
}

bool assemblyPreviewCheck(const QString &databasePath)
{
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(databasePath) && repository.initialize());
    EquipmentController controller(repository);
    EquipmentWindow window(controller);
    REQUIRE(window.loadEquipment(QStringLiteral("EQ-001")));
    if (QApplication::platformName() == QStringLiteral("windows")) window.setWindowFlag(Qt::WindowStaysOnTopHint, true);
    window.show(); QTest::qWait(160);
    REQUIRE(importAndWait(window, assemblyPath)); // 实际STEP转换→事务保存→完整读回，绝不塞入预制JSON。
    const QString sourceSha = sourceHash(assemblyPath);
    const QString equipmentId = QStringLiteral("CAD-") + sourceSha;
    if (!controller.equipment() || controller.equipment()->id != equipmentId || controller.parts().size() != expectedAssembly) {
        auto *cad = worker(window);
        QTextStream(stderr) << "CAD_ASSEMBLY_IMPORT_DIAGNOSTIC expectedId=" << equipmentId
            << " currentId=" << (controller.equipment() ? controller.equipment()->id : QStringLiteral("<none>"))
            << " parts=" << controller.parts().size() << " expectedParts=" << expectedAssembly
            << " status='" << window.findChild<QLabel *>(QStringLiteral("equipmentStatus"))->text()
            << "' workerError='" << (cad ? cad->lastError() : QStringLiteral("<none>"))
            << "' job='" << (cad ? cad->jobExitDiagnostic() : QStringLiteral("<none>")) << "'" << Qt::endl;
    }
    REQUIRE(controller.equipment() && controller.equipment()->id == equipmentId && controller.parts().size() == expectedAssembly);
    const QList<Part> source = controller.parts();
    const QStringList original = fingerprints(source);
    REQUIRE(original.size() == expectedAssembly);
    quint64 vertices = 0, triangles = 0;
    QString selected;
    double largestVolume = -1;
    for (const Part &part : source) {
        REQUIRE(part.mesh && part.shape == QStringLiteral("mesh") && part.cadSourceSha256 == sourceSha);
        vertices += part.mesh->vertices.size(); triangles += part.mesh->triangles.size();
        const double volume = part.sizeX.value_or(0) * part.sizeY.value_or(0) * part.sizeZ.value_or(0);
        if (volume > largestVolume) { largestVolume = volume; selected = part.id; }
    }
    auto *scene = dynamic_cast<SceneView *>(window.findChild<QWidget *>(QStringLiteral("sceneView")));
    REQUIRE(scene && !selected.isEmpty() && vertices > quint64(expectedAssembly * 20) && triangles > quint64(expectedAssembly * 20));
    auto *canvas = scene->findChild<QWidget *>(QStringLiteral("sceneCanvas"));
    REQUIRE(canvas);
    if (QApplication::platformName() == QStringLiteral("windows")) {
        auto *native = dynamic_cast<QOpenGLWidget *>(canvas);
        REQUIRE(native && waitUntil([&] { return native->isValid(); }, 5000));
    }
    auto *view = window.findChild<QComboBox *>(QStringLiteral("viewModeSelector"));
    auto *projection = window.findChild<QComboBox *>(QStringLiteral("projectionSelector"));
    auto *axes = window.findChild<QComboBox *>(QStringLiteral("axisPlacementSelector"));
    auto *explodeAxis = window.findChild<QComboBox *>(QStringLiteral("explodeAxisSelector"));
    auto *dimensions = window.findChild<QCheckBox *>(QStringLiteral("dimensionsCheck"));
    auto *grid = window.findChild<QCheckBox *>(QStringLiteral("gridCheck"));
    auto *explosion = window.findChild<QSlider *>(QStringLiteral("explosionSlider"));
    auto *pull = window.findChild<QSlider *>(QStringLiteral("pullSlider"));
    auto *restore = window.findChild<QPushButton *>(QStringLiteral("restoreDisplayButton"));
    auto *restorePart = window.findChild<QPushButton *>(QStringLiteral("restorePartButton"));
    auto *isolate = window.findChild<QPushButton *>(QStringLiteral("isolateSelectedButton"));
    auto *showAll = window.findChild<QPushButton *>(QStringLiteral("showAllButton"));
    REQUIRE(view && projection && axes && explodeAxis && dimensions && grid && explosion && pull && restore && restorePart && isolate && showAll);
    REQUIRE(view->currentData().toInt() == 0 && scene->viewMode() == SceneView::ViewMode::Isometric && !dimensions->isChecked() && !scene->dimensionsVisible());
    REQUIRE(clickPart(window, selected));
    QSet<QRgb> colors;
    for (const Part &part : source) colors.insert(scene->partColor(part.id).rgb());
    REQUIRE(colors.size() == source.size());
    REQUIRE(capture(window, QStringLiteral("assembly-01-colored-source.png")));
    dimensions->setFocus(); QTest::keyClick(dimensions, Qt::Key_Space); drain();
    REQUIRE(dimensions->isChecked() && scene->dimensionsVisible());
    int dimensionAxes = 0;
    for (const auto &annotation : scene->measurementAnnotations()) if (annotation.axis.size() == 1 && QStringLiteral("XYZ").contains(annotation.axis)) ++dimensionAxes;
    REQUIRE(dimensionAxes == 3);
    REQUIRE(chooseCombo(axes, int(SceneView::AxisPlacement::Both)) && scene->axisPlacement() == SceneView::AxisPlacement::Both);
    REQUIRE(chooseCombo(view, 1) && scene->viewMode() == SceneView::ViewMode::Engineering);
    REQUIRE(capture(window, QStringLiteral("assembly-02-engineering-dimensions.png")));
    const QImage engineering = scenePixels(*scene); REQUIRE(!engineering.isNull());
    REQUIRE(chooseCombo(view, 2) && scene->viewMode() == SceneView::ViewMode::Front);
    REQUIRE(chooseCombo(projection, int(SceneView::ViewMode::Top)) && scene->viewMode() == SceneView::ViewMode::Top);
    REQUIRE(capture(window, QStringLiteral("assembly-03-projection-top.png")));
    REQUIRE(scenePixels(*scene) != engineering);
    REQUIRE(chooseCombo(view, 3) && scene->viewMode() == SceneView::ViewMode::ThreeViews);
    REQUIRE(capture(window, QStringLiteral("assembly-04-three-views-dimensions.png")));
    dimensions->setFocus(); QTest::keyClick(dimensions, Qt::Key_Space);
    grid->setFocus(); QTest::keyClick(grid, Qt::Key_Space); drain(); QTest::qWait(80);
    REQUIRE(!scene->dimensionsVisible() && !scene->gridVisible());
    REQUIRE(chooseCombo(axes, int(SceneView::AxisPlacement::Hidden)));
    const QImage threeViews = scenePixels(*scene);
    REQUIRE(!threeViews.isNull());
    // 三个实帧中央区域均需有几何像素；关闭网格/尺寸/XYZ排除仅文字或参考线的假证据。
    for (int panel = 0; panel < 3; ++panel) {
        int geometryPixels = 0;
        for (int y = threeViews.height() / 4; y < threeViews.height() * 4 / 5; y += 2)
            for (int x = threeViews.width() * panel / 3 + 8; x < threeViews.width() * (panel + 1) / 3 - 8; x += 2) {
                const QColor pixel = threeViews.pixelColor(x, y);
                if (pixel.red() > 70 || pixel.green() > 80 || pixel.blue() > 90) ++geometryPixels;
            }
        REQUIRE(geometryPixels > 20);
        QTextStream(stdout) << "CAD_ASSEMBLY_FRAME panel=" << panel << " geometryPixels=" << geometryPixels << Qt::endl;
    }
    REQUIRE(capture(window, QStringLiteral("assembly-05-three-views-unobscured.png")));
    REQUIRE(chooseCombo(view, 0));
    for (const auto axis : {SceneView::ExplodeAxis::Auto, SceneView::ExplodeAxis::X, SceneView::ExplodeAxis::Y, SceneView::ExplodeAxis::Z}) {
        QTest::mouseClick(restore, Qt::LeftButton); drain();
        REQUIRE(chooseCombo(explodeAxis, int(axis)) && sliderMaximum(explosion));
        REQUIRE(scene->explosionFraction() == 1);
        const int active = int(scene->resolvedExplodeAxis()) - int(SceneView::ExplodeAxis::X);
        REQUIRE(active >= 0 && active < 3);
        long double low = std::numeric_limits<long double>::infinity(), high = -low;
        for (const Part &part : source) {
            const std::array<long double, 3> position{part.positionX, part.positionY, part.positionZ};
            const std::array<long double, 3> size{part.sizeX.value_or(0), part.sizeY.value_or(0), part.sizeZ.value_or(0)};
            low = std::min(low, position[active] - size[active] / 2);
            high = std::max(high, position[active] + size[active] / 2);
        }
        const long double sourceCenter = (low + high) / 2;
        QList<QPair<long double, long double>> ordering;
        int moved = 0;
        for (const Part &part : source) {
            const std::array<long double, 3> position{part.positionX, part.positionY, part.positionZ};
            const auto offset = scene->displayOffset(part.id);
            for (int other = 0; other < 3; ++other) if (other != active) REQUIRE(std::abs(offset[other]) < .001L);
            if (position[active] < sourceCenter - .001L) REQUIRE(offset[active] <= .001L);
            if (position[active] > sourceCenter + .001L) REQUIRE(offset[active] >= -.001L);
            if (std::abs(offset[active]) > .001L) ++moved;
            ordering.append(qMakePair(position[active], position[active] + offset[active]));
        }
        std::sort(ordering.begin(), ordering.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        for (int index = 1; index < ordering.size(); ++index)
            if (ordering[index].first - ordering[index - 1].first > .001L)
                REQUIRE(ordering[index].second - ordering[index - 1].second >= ordering[index].first - ordering[index - 1].first - .001L);
        if (axis == SceneView::ExplodeAxis::Auto) REQUIRE(moved > 0);
        REQUIRE(fingerprints(controller.parts()) == original);
    }
    REQUIRE(chooseCombo(explodeAxis, int(SceneView::ExplodeAxis::Auto)));
    QHash<QString, std::array<long double, 3>> beforePull;
    for (const Part &part : source) beforePull.insert(part.id, scene->displayOffset(part.id));
    REQUIRE(sliderMaximum(pull) && scene->pullFraction() == 1);
    REQUIRE(scene->displayOffset(selected) != beforePull.value(selected));
    for (const Part &part : source) if (part.id != selected) REQUIRE(scene->displayOffset(part.id) == beforePull.value(part.id));
    REQUIRE(fingerprints(controller.parts()) == original);
    REQUIRE(capture(window, QStringLiteral("assembly-06-ordered-expand-single-pull.png")));
    QTest::mouseClick(restorePart, Qt::LeftButton); drain();
    REQUIRE(scene->pullFraction() == 0 && scene->displayOffset(selected) == beforePull.value(selected));
    QTest::mouseClick(restore, Qt::LeftButton); drain();
    REQUIRE(scene->explosionFraction() == 0 && scene->pullFraction() == 0 && explosion->value() == 0 && pull->value() == 0);
    for (const Part &part : source) for (const auto offset : scene->displayOffset(part.id)) REQUIRE(offset == 0);
    REQUIRE(fingerprints(controller.parts()) == original);
    QTest::mouseClick(isolate, Qt::LeftButton); drain();
    const QSet<QString> hidden = controller.hiddenPartIds();
    REQUIRE(controller.isIsolationActive() && hidden.size() == expectedAssembly - 1 && !hidden.contains(selected));
    REQUIRE(chooseCombo(view, 3));
    // 实际画布边角无模型；单击不清，双击清选择且保控制器唯一隔离目标。
    const QPoint blank(4, canvas->height() - 4);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, blank); drain();
    REQUIRE(controller.selectedPartId() == selected && controller.hiddenPartIds() == hidden);
    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, blank);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, blank); drain();
    REQUIRE(controller.selectedPartId().isEmpty() && controller.isIsolationActive() && controller.hiddenPartIds() == hidden);
    REQUIRE(controller.isolatedPartId() == selected);
    REQUIRE(capture(window, QStringLiteral("assembly-07-blank-clear-keeps-single.png")));
    REQUIRE(clickPart(window, source.first().id == selected ? source.last().id : source.first().id));
    REQUIRE(controller.isIsolationActive() && !controller.hiddenPartIds().contains(controller.selectedPartId()));
    QTest::mouseClick(showAll, Qt::LeftButton); drain();
    REQUIRE(!controller.isIsolationActive() && controller.hiddenPartIds().isEmpty());
    QList<Part> persisted;
    REQUIRE(repository.loadParts(equipmentId, persisted) && fingerprints(persisted) == original);
    EquipmentRepository restarted;
    REQUIRE(restarted.openDatabase(databasePath) && restarted.initialize());
    QList<Part> restartParts;
    REQUIRE(restarted.loadParts(equipmentId, restartParts) && fingerprints(restartParts) == original);
    QTextStream(stdout) << "CAD_ASSEMBLY_PREVIEW parts=" << source.size() << " vertices=" << vertices << " triangles=" << triangles
        << " realStepImport=1 controlsByKeyboardMouse=1 fourViews=1 threePanelsGeometry=1 orderedAxis=1 pullOnlySelected=1 restoreSourceExact=1"
        << " blankClearKeepsIsolation=1 databaseRestartExact=1 native=" << (QApplication::platformName() == QStringLiteral("windows")) << " agentTrial=1 realUserAcceptance=0" << Qt::endl;
    return true;
}

bool functional(const QString &badFile, QString &databasePath, QStringList &assemblyFingerprint, QString &assemblyId)
{
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(databasePath) && repository.initialize());
    EquipmentController controller(repository);
    EquipmentWindow window(controller);
    REQUIRE(window.loadEquipment(QStringLiteral("EQ-001")));
    if (QApplication::platformName() == QStringLiteral("windows")) window.setWindowFlag(Qt::WindowStaysOnTopHint, true);
    window.show(); QTest::qWait(200);
    auto *reset = window.findChild<QPushButton *>(QStringLiteral("fitAllButton"));
    auto *cancel = window.findChild<QPushButton *>(QStringLiteral("cadCancelButton"));
    auto *import = window.findChild<QPushButton *>(QStringLiteral("importButton"));
    auto *scene = dynamic_cast<SceneView *>(window.findChild<QWidget *>(QStringLiteral("sceneView")));
    REQUIRE(reset && cancel && import && scene);
    auto *canvas = scene->findChild<QWidget *>(QStringLiteral("sceneCanvas"));
    REQUIRE(canvas && reset->text() == QStringLiteral("视角回正") && import->text().contains(QStringLiteral("STEP")));
    REQUIRE(importAndWait(window, bearingPath));
    REQUIRE(controller.equipment()->id == QStringLiteral("CAD-") + sourceHash(bearingPath) && controller.parts().size() == 1);
    const Part bearing = controller.parts().first();
    REQUIRE(meshFields(bearing, bearingPath) && bearingHole(bearing));
    const Triple dimensions{*bearing.sizeX, *bearing.sizeY, *bearing.sizeZ};
    for (int axis = 0; axis < 3; ++axis) REQUIRE(std::abs(dimensions[axis] - expectedBearing[axis]) < 0.02);
    // 真实画布旋转到轴承端面；中心应透孔且空点击清选择，外环点击选中真实网格。
    drag(canvas, QPoint(0, 120));
    const QImage ring = scenePixels(*scene);
    REQUIRE(!ring.isNull());
    const double dpr = ring.devicePixelRatio();
    const QPoint center = (QPointF(canvas->rect().center()) * dpr).toPoint();
    REQUIRE(ring.pixelColor(center) == QColor(31, 40, 52));
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    REQUIRE(controller.selectedPartId().isEmpty());
    QPoint annulus(-1, -1);
    for (int x = center.x() + 5; x < ring.width() - 5; ++x) {
        const QPoint point(x, center.y());
        if (blue(ring.pixelColor(point)) && blue(ring.pixelColor(point + QPoint(2, 0)))
            && blue(ring.pixelColor(point + QPoint(0, 2))) && blue(ring.pixelColor(point - QPoint(0, 2)))) {
            annulus = point + QPoint(2, 0); break;
        }
    }
    REQUIRE(annulus.x() >= 0);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, (QPointF(annulus) / dpr).toPoint());
    REQUIRE(controller.selectedPartId() == bearing.id && displayedPart(window, *scene, bearing));
    REQUIRE(orange(scenePixels(*scene).pixelColor(annulus)));
    REQUIRE(capture(window, QStringLiteral("01-bearing-real-hole-properties-mm.png")));
    QTextStream(stdout) << "CAD_USER bearingRealHole=1 annulusMousePick=1 sourceSha=1 bboxMm=1 vertices=" << bearing.mesh->vertices.size()
                        << " triangles=" << bearing.mesh->triangles.size() << Qt::endl;

    REQUIRE(importAndWait(window, assemblyPath));
    assemblyId = QStringLiteral("CAD-") + sourceHash(assemblyPath);
    REQUIRE(controller.equipment()->id == assemblyId && controller.parts().size() == expectedAssembly);
    quint64 vertices = 0, triangles = 0;
    QSet<QString> centers;
    QString biggest, smallest;
    double bigVolume = -1, smallVolume = std::numeric_limits<double>::infinity();
    for (const auto &part : controller.parts()) {
        REQUIRE(meshFields(part, assemblyPath));
        vertices += quint64(part.mesh->vertices.size()); triangles += quint64(part.mesh->triangles.size());
        centers.insert(QStringLiteral("%1/%2/%3").arg(part.positionX, 0, 'g', 17).arg(part.positionY, 0, 'g', 17).arg(part.positionZ, 0, 'g', 17));
        const double volume = *part.sizeX * *part.sizeY * *part.sizeZ;
        if (volume > bigVolume) { bigVolume = volume; biggest = part.id; }
        if (volume < smallVolume) { smallVolume = volume; smallest = part.id; }
    }
    REQUIRE(vertices > quint64(expectedAssembly * 20) && triangles > quint64(expectedAssembly * 20) && centers.size() > 1 && biggest != smallest);
    assemblyFingerprint = fingerprints(controller.parts());
    REQUIRE(assemblyFingerprint.size() == expectedAssembly && clickPart(window, biggest));
    REQUIRE(displayedPart(window, *scene, *controller.selectedPart()));
    REQUIRE(capture(window, QStringLiteral("02-assembly-positioned-parts.png")));
    auto *isolate = window.findChild<QPushButton *>(QStringLiteral("isolateSelectedButton"));
    auto *showAll = window.findChild<QPushButton *>(QStringLiteral("showAllButton"));
    REQUIRE(isolate && showAll);
    QTest::mouseClick(isolate, Qt::LeftButton);
    const auto firstHidden = controller.hiddenPartIds();
    REQUIRE(controller.isIsolationActive() && firstHidden.size() == expectedAssembly - 1 && !firstHidden.contains(biggest));
    // 1.1有效单击直接切隔离，不需第二次双击；原位/网格参数仍受后续指纹校验保护。
    REQUIRE(clickPart(window, smallest) && controller.hiddenPartIds() != firstHidden
            && controller.hiddenPartIds().size() == expectedAssembly - 1 && !controller.hiddenPartIds().contains(smallest));
    REQUIRE(clickPart(window, smallest, true));
    REQUIRE(controller.isIsolationActive() && controller.selectedPartId() == smallest
            && controller.hiddenPartIds().size() == expectedAssembly - 1 && !controller.hiddenPartIds().contains(smallest));
    REQUIRE(displayedPart(window, *scene, *controller.selectedPart()));
    REQUIRE(capture(window, QStringLiteral("03-assembly-thin-piece-double-isolation.png")));
    auto *search = window.findChild<QLineEdit *>(QStringLiteral("partSearch"));
    REQUIRE(pasteSearch(search, smallest));
    REQUIRE(window.findChild<QListWidget *>(QStringLiteral("partsList"))->count() == 1 && controller.selectedPartId() == smallest);
    REQUIRE(pasteSearch(search, QString()));
    QTest::mouseClick(reset, Qt::LeftButton); QTest::qWait(60);
    const auto baseline = scenePixels(*scene);
    drag(canvas, QPoint(60, 25));
    REQUIRE(scenePixels(*scene) != baseline);
    QWheelEvent wheel(QPointF(canvas->rect().center()), QPointF(canvas->mapToGlobal(canvas->rect().center())), QPoint(), QPoint(0, 240),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(canvas, &wheel); QTest::qWait(60);
    REQUIRE(capture(window, QStringLiteral("04-cad-rotated-zoomed.png")));
    QTest::mouseClick(reset, Qt::LeftButton); QTest::qWait(60);
    REQUIRE(scenePixels(*scene) == baseline && controller.isIsolationActive() && controller.selectedPartId() == smallest);
    REQUIRE(capture(window, QStringLiteral("05-cad-camera-reset-keeps-isolation.png")));
    REQUIRE(failedImportPreserves(window, controller, badFile));
    if (!rejectedPath.isEmpty()) REQUIRE(failedImportPreserves(window, controller, rejectedPath));
    REQUIRE(failedImportPreserves(window, controller, assemblyPath)); // 重复源SHA不能覆盖已保存网格。
    REQUIRE(capture(window, QStringLiteral("06-failed-or-duplicate-keeps-mesh.png")));

    const QString cancelPath = rejectedPath.isEmpty() ? assemblyPath : rejectedPath;
    REQUIRE(window.importFile(cancelPath) && window.cadImportInProgress() && cancel->isEnabled() && !import->isEnabled() && reset->isEnabled());
    auto *cad = worker(window);
    REQUIRE(cad);
    REQUIRE(waitUntil([&] { return cad->processId() > 0 && cad->isBusy(); }, 3000));
#ifdef Q_OS_WIN
    REQUIRE(waitUntil([&] { return cad->isConfined(); }, 3000));
#endif
    QTest::mouseClick(reset, Qt::LeftButton); // 转换等待期间仍能使用当前视图。
    REQUIRE(window.cadImportInProgress() && cad->isBusy() && cancel->isEnabled());
    QTest::mouseClick(cancel, Qt::LeftButton);
    REQUIRE(waitUntil([&] { return !window.cadImportInProgress(); }, 5000));
    REQUIRE(!cad->isBusy() && cad->processId() == 0 && import->isEnabled() && !cancel->isEnabled());
    REQUIRE(window.findChild<QLabel *>(QStringLiteral("equipmentStatus"))->text().contains(QStringLiteral("取消")));
    REQUIRE(controller.equipment()->id == assemblyId && fingerprints(controller.parts()) == assemblyFingerprint && controller.selectedPartId() == smallest);
    REQUIRE(clickPart(window, biggest, true) && !controller.hiddenPartIds().contains(biggest));
    REQUIRE(capture(window, QStringLiteral("07-cancelled-still-interactive.png")));

    QTest::mouseClick(showAll, Qt::LeftButton);
    REQUIRE(controller.hiddenPartIds().isEmpty() && !controller.isIsolationActive());

    const QString legacyFile = QFileInfo(badFile).dir().filePath(QStringLiteral("legacy.json"));
    QFile legacy(legacyFile);
    REQUIRE(legacy.open(QIODevice::WriteOnly));
    legacy.write(R"({"equipmentId":"LEGACY-CAD-TEST","equipmentName":"原JSON仍可导入","parts":[{"id":"L-1","name":"基础零件","material":"Q235","shape":"box","dimensions":[100,80,60],"position":[0,0,0]}]})");
    legacy.close();
    REQUIRE(window.importFile(legacyFile) && controller.equipment()->id == QStringLiteral("LEGACY-CAD-TEST") && !controller.parts().first().mesh);
    REQUIRE(window.loadEquipment(assemblyId) && fingerprints(controller.parts()) == assemblyFingerprint);
    REQUIRE(capture(window, QStringLiteral("09-cad-reloaded-after-legacy-json.png")));
    QTextStream(stdout) << "CAD_USER assemblyParts=" << expectedAssembly << " vertices=" << vertices << " triangles=" << triangles
        << " placedCenters=" << centers.size() << " doubleIsolation=1 search=1 reset=1 failedKeepsMesh=1 cancelledPidConfirmed=1 legacyJson=1 renderer='"
        << scene->rendererDescription() << "'" << Qt::endl;
    return true;
}

struct Resources { bool valid = false; quint64 privateBytes = 0; quint32 handles = 0; };
Resources resources()
{
    Resources result;
#ifdef Q_OS_WIN
    using ReadMemory = BOOL (WINAPI *)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    const auto readMemory = reinterpret_cast<ReadMemory>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo"));
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    DWORD handles = 0;
    result.valid = readMemory && readMemory(GetCurrentProcess(), reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&memory), sizeof(memory))
        && GetProcessHandleCount(GetCurrentProcess(), &handles);
    result.privateBytes = quint64(memory.PrivateUsage); result.handles = handles;
#endif
    return result;
}

bool resourceRun(const QString &badFile)
{
    Resources baseline;
    for (int cycle = 0; cycle <= resourceCycles; ++cycle) {
        QTemporaryDir data;
        REQUIRE(data.isValid());
        QPointer<EquipmentWindow> gone;
        {
            EquipmentRepository repository;
            REQUIRE(repository.openDatabase(data.filePath(QStringLiteral("repeat.sqlite"))) && repository.initialize());
            EquipmentController controller(repository);
            auto window = std::make_unique<EquipmentWindow>(controller);
            gone = window.get();
            window->show(); drain();
            REQUIRE(window->loadEquipment(QStringLiteral("EQ-001")) && importAndWait(*window, bearingPath));
            REQUIRE(controller.parts().size() == 1 && meshFields(controller.parts().first(), bearingPath));
            REQUIRE(failedImportPreserves(*window, controller, badFile));
            auto *cad = worker(*window);
            REQUIRE(cad && !cad->isBusy() && cad->processId() == 0);
            window.reset();
        }
        drain(); QTest::qWait(80);
        REQUIRE(gone.isNull());
        const auto sample = resources();
#ifdef Q_OS_WIN
        REQUIRE(sample.valid); // Windows不能将未取得的0计数包装成资源验收通过。
#endif
        if (cycle == 0) baseline = sample; // 一次真实正常/失败导入销毁预热，不把首次Qt/驱动缓存算泄漏。
        QTextStream(stdout) << "CAD_RESOURCE cycle=" << cycle << " valid=" << sample.valid << " privateBytes=" << sample.privateBytes
                            << " handles=" << sample.handles << " workerPidIdle=1 objectsReleased=1" << Qt::endl;
        if (sample.valid && baseline.valid) {
            REQUIRE(sample.handles <= baseline.handles + 32);
            REQUIRE(sample.privateBytes <= baseline.privateBytes + 96ULL * 1024 * 1024);
        }
    }
    QTextStream(stdout) << "CAD_RESOURCE cycles=" << resourceCycles << " afterWarmup=1 accepted=1" << Qt::endl;
    return true;
}

bool sqliteRecovery(const QString &source, const QString &assemblyId, const QStringList &expected, QTemporaryDir &owner)
{
    const QDir directory(QFileInfo(source).absolutePath());
    const QString script = directory.filePath(QStringLiteral("cad-sqlite-recovery.py"));
    const QString snapshot = directory.filePath(QStringLiteral("cad-backup.sqlite"));
    const QString restored = directory.filePath(QStringLiteral("cad-restored.sqlite"));
    QFile helper(script);
    REQUIRE(helper.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    const QByteArray code = R"py(from contextlib import closing
from pathlib import Path
import sqlite3
import sys

source, snapshot, restored = map(lambda x: Path(x).resolve(), sys.argv[1:])
assert source.is_file() and source.parent == snapshot.parent == restored.parent
assert snapshot.name == 'cad-backup.sqlite' and restored.name == 'cad-restored.sqlite'
assert not snapshot.exists() and not restored.exists()
with closing(sqlite3.connect(source.as_uri() + '?mode=ro', uri=True, timeout=5)) as original:
    with closing(sqlite3.connect(snapshot, timeout=5)) as target:
        original.backup(target, pages=64, sleep=0.01)
        assert target.execute('PRAGMA quick_check').fetchall() == [('ok',)]
        assert target.execute('PRAGMA foreign_key_check').fetchall() == []
with closing(sqlite3.connect(snapshot.as_uri() + '?mode=ro', uri=True, timeout=5)) as original:
    with closing(sqlite3.connect(restored, timeout=5)) as target:
        original.backup(target, pages=64, sleep=0.01)
        assert target.execute('PRAGMA quick_check').fetchall() == [('ok',)]
        assert target.execute('PRAGMA foreign_key_check').fetchall() == []
print('CAD_SQLITE_RECOVERY backup=1 restored=1 quickCheck=1 foreignKeys=1', flush=True)
)py";
    REQUIRE(helper.write(code) == code.size()); helper.close();
    // 新helper仅私有Python stdlib SQLite；source为本测试临时库且Qt新连接仍打开。
    QProcess process;
    process.start(CadImportProcess::Config::runtime().python, {QStringLiteral("-I"), script, source, snapshot, restored});
    REQUIRE(process.waitForStarted(5000));
    if (!process.waitForFinished(15000)) {
        process.kill();
        if (!process.waitForFinished(3000)) {
            owner.setAutoRemove(false);
            QTextStream(stderr) << "CAD_SQLITE_RECOVERY exitUnconfirmed=1 ownedPid=" << process.processId() << " retained=" << owner.path() << Qt::endl;
            return false;
        }
        return false;
    }
    REQUIRE(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0);
    REQUIRE(process.readAllStandardOutput().contains("CAD_SQLITE_RECOVERY backup=1 restored=1"));
    EquipmentRepository database;
    REQUIRE(database.openDatabase(restored) && database.initialize());
    EquipmentController controller(database);
    REQUIRE(controller.loadEquipment(assemblyId) && fingerprints(controller.parts()) == expected);
    QTextStream(stdout) << "CAD_SQLITE_RECOVERY onlineBackup=1 restoredNewRepository=1 meshAndSourceFingerprintExact=1 temporaryOnly=1" << Qt::endl;
    return true;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const auto arguments = app.arguments();
    const bool assemblyPreview = arguments.contains(QStringLiteral("--assembly-preview-check"));
    auto option = [&](const QString &name) {
        const int index = arguments.indexOf(name);
        return index >= 0 && index + 1 < arguments.size() ? arguments[index + 1] : QString();
    };
    bearingPath = QFileInfo(option(QStringLiteral("--bearing"))).absoluteFilePath();
    assemblyPath = QFileInfo(option(QStringLiteral("--assembly"))).absoluteFilePath();
    expectedAssembly = option(QStringLiteral("--expected-assembly-count")).toInt();
    const auto rejected = option(QStringLiteral("--rejected-step"));
    if (!rejected.isEmpty()) rejectedPath = QFileInfo(rejected).absoluteFilePath();
    resourceCycles = option(QStringLiteral("--resource-cycles")).toInt();
    const auto expected = option(QStringLiteral("--bearing-dimensions"));
    if (!expected.isEmpty()) {
        const auto values = expected.split(QLatin1Char(','));
        if (values.size() != 3) return 1;
        for (int axis = 0; axis < 3; ++axis) { bool ok = false; expectedBearing[axis] = values[axis].toDouble(&ok); if (!ok || expectedBearing[axis] <= 0) return 1; }
    }
    for (const auto &pair : {qMakePair(QStringLiteral("--python"), QByteArray("EQUIPMENT_CAD_PYTHON")),
                             qMakePair(QStringLiteral("--modules"), QByteArray("EQUIPMENT_CAD_MODULES")),
                             qMakePair(QStringLiteral("--converter"), QByteArray("EQUIPMENT_CAD_CONVERTER"))}) {
        const auto value = option(pair.first);
        if (!value.isEmpty()) qputenv(pair.second.constData(), QFileInfo(value).absoluteFilePath().toUtf8());
    }
    const auto runtime = CadImportProcess::Config::runtime();
    if ((!assemblyPreview && !QFileInfo(bearingPath).isFile()) || !QFileInfo(assemblyPath).isFile() || expectedAssembly < 2 || expectedAssembly > 1000
        || resourceCycles < 0 || resourceCycles > 10 || (!rejectedPath.isEmpty() && !QFileInfo(rejectedPath).isFile())
        || !QFileInfo(runtime.python).isAbsolute() || !QFileInfo(runtime.python).isFile()
        || !QFileInfo(runtime.modules).isAbsolute() || !QFileInfo(runtime.modules).isDir()
        || !QFileInfo(runtime.converter).isAbsolute() || !QFileInfo(runtime.converter).isFile()) {
        QTextStream(stderr) << "CAD_USER invalid arguments/runtime; require explicit bearing/assembly/expected count and trusted private runtime." << Qt::endl;
        return 1;
    }
    const auto destination = option(QStringLiteral("--snapshot-dir"));
    if (!destination.isEmpty()) { snapshots = QDir(destination).absolutePath(); if (!QDir().mkpath(snapshots)) return 1; }
    // 无HTTP测试，仅本地明确拒绝CAD发布；不加载正常服务令牌配置。
    qputenv("EQUIPMENT_SERVICE_CONFIG", QByteArray("__cad_user_no_service_config__"));
    QTemporaryDir data;
    if (!data.isValid()) return 1;
    const QString badFile = data.filePath(QStringLiteral("invalid.step"));
    QFile invalid(badFile);
    if (!invalid.open(QIODevice::WriteOnly)) return 1;
    invalid.write("ISO-10303-21;\nINVALID CONTROLLED STEP;\nEND-ISO-10303-21;\n"); invalid.close();
    QString database = data.filePath(QStringLiteral("cad-user.sqlite"));
    // 1.1有限专项仅导入本轮6/82真实装配并验新控件，不重复旧轴承/取消/长资源循环。
    if (assemblyPreview) return assemblyPreviewCheck(database) ? 0 : 1;
    QString assemblyId;
    QStringList assemblyFingerprint;
    if (!functional(badFile, database, assemblyFingerprint, assemblyId)) return 1;
    // 前一Window/Controller/Repository已经全部析构，以新连接模拟重启同一临时SQLite。
    EquipmentRepository reopened;
    if (!reopened.openDatabase(database) || !reopened.initialize()) return 1;
    EquipmentController controller(reopened);
    EquipmentWindow window(controller);
    if (!window.loadEquipment(assemblyId) || fingerprints(controller.parts()) != assemblyFingerprint) return 1;
    if (QApplication::platformName() == QStringLiteral("windows")) window.setWindowFlag(Qt::WindowStaysOnTopHint, true);
    window.show(); QTest::qWait(80);
    if (!clickPart(window, controller.parts().first().id) || !capture(window, QStringLiteral("10-reopened-sqlite-real-mesh.png"))) return 1;
    window.close();
    if (!sqliteRecovery(database, assemblyId, assemblyFingerprint, data)) return 1;
    if (resourceCycles > 0 && !resourceRun(badFile)) return 1;
    QTextStream(stdout) << "CAD_USER restartedSameSqlite=1 meshFingerprintExact=1 publicStepInputs=1 acceptance=1 agentTrial=1 realRequirementR08=1" << Qt::endl;
    return 0;
}
