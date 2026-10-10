#include "equipmentcontroller.h"
#include "equipmentwindow.h"
#include "sceneview.h"

#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QWheelEvent>
#include <functional>

// 真实用户0.7反馈的自动控件验收；测试结果不是用户本人再次操作/验收。
namespace {
const QString equipmentId = QStringLiteral("R07-EQ");
const QString aId = QStringLiteral("R07-A");
const QString bId = QStringLiteral("R07-B ");
const QString cId = QStringLiteral("R07-C");
QString snapshots;

#define REQUIRE(condition) do { if (!(condition)) { \
    QTextStream(stderr) << "R07 WINDOW FAIL " << __LINE__ << ": " << #condition << Qt::endl; \
    return false; } } while (false)

bool waitUntil(const std::function<bool()> &condition, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents();
        if (condition()) return true;
        QTest::qWait(20);
    } while (timer.elapsed() < timeoutMs);
    return false;
}

QByteArray fixture()
{
    const QJsonArray parts{
        QJsonObject{{"id", aId}, {"name", QStringLiteral("同名零件")}, {"material", QStringLiteral("Q235")},
                    {"shape", "box"}, {"dimensions", QJsonArray{100, 80, 60}}, {"position", QJsonArray{0, 0, 0}}},
        QJsonObject{{"id", bId}, {"name", QStringLiteral("同名零件")}, {"material", QStringLiteral("45钢")},
                    {"shape", "cylinder"}, {"dimensions", QJsonArray{10, 60}}, {"position", QJsonArray{180, 0, 0}}},
        QJsonObject{{"id", cId}, {"name", QStringLiteral("同名零件")}, {"material", QStringLiteral("铝")},
                    {"shape", "box"}, {"dimensions", QJsonArray{70, 40, 30}}, {"position", QJsonArray{-180, 0, 0}}}
    };
    return QJsonDocument(QJsonObject{{"equipmentId", equipmentId}, {"equipmentName", QStringLiteral("真实反馈验收样例")},
                                    {"parts", parts}}).toJson(QJsonDocument::Compact);
}

bool capture(EquipmentWindow &window, const QString &name)
{
    QCoreApplication::processEvents();
    if (snapshots.isEmpty()) return true;
    if (QApplication::platformName() == QStringLiteral("windows")) {
        // QWidget::grab可能漏OpenGL内QPainter叠加；只截自己已显示窗口，保留真实屏幕标尺。
        window.raise();
        window.activateWindow();
        QTest::qWait(80);
        REQUIRE(window.screen());
        const QPixmap pixels = window.screen()->grabWindow(window.winId());
        return !pixels.isNull() && pixels.save(QDir(snapshots).filePath(name));
    }
    return window.grab().save(QDir(snapshots).filePath(name));
}

QImage scenePixels(SceneView &scene)
{
    auto *canvas = scene.findChild<QWidget *>(QStringLiteral("sceneCanvas"));
    if (auto *native = dynamic_cast<QOpenGLWidget *>(canvas)) return native->grabFramebuffer();
    return canvas ? canvas->grab().toImage() : QImage();
}

bool partPoint(QListWidget *list, const QString &id, QPoint &point)
{
    for (int row = 0; row < list->count(); ++row) {
        const auto *item = list->item(row);
        if (item->data(Qt::UserRole).toString() == id) {
            list->scrollToItem(list->item(row));
            const QRect rectangle = list->visualItemRect(list->item(row));
            REQUIRE(!rectangle.isEmpty());
            point = rectangle.center();
            return true;
        }
    }
    return false;
}

bool clickPart(QListWidget *list, const QString &id, bool doubleClick = false)
{
    QPoint point;
    REQUIRE(partPoint(list, id, point));
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QCoreApplication::processEvents();
    if (doubleClick) {
        // 单击与双击槽可能重建列表；每次输入前按ID重取坐标，不跨事件保留item指针。
        REQUIRE(partPoint(list, id, point));
        QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::qWait(30);
        QCoreApplication::processEvents();
    }
    return true;
}

void searchText(QLineEdit *search, const QString &text)
{
    QTest::mouseClick(search, Qt::LeftButton);
    QTest::keyClick(search, Qt::Key_A, Qt::ControlModifier);
    if (text.isEmpty()) QTest::keyClick(search, Qt::Key_Backspace);
    else QTest::keyClicks(search, text);
    QCoreApplication::processEvents();
}

bool windowBehavior()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(temporary.filePath(QStringLiteral("r07-window.sqlite"))) && repository.initialize());
    EquipmentController controller(repository);
    EquipmentWindow window(controller);
    REQUIRE(window.importData(fixture()));
    window.show();
    QTest::qWait(250);
    auto *list = window.findChild<QListWidget *>(QStringLiteral("partsList"));
    auto *search = window.findChild<QLineEdit *>(QStringLiteral("partSearch"));
    auto *id = window.findChild<QLabel *>(QStringLiteral("partId"));
    auto *material = window.findChild<QLabel *>(QStringLiteral("material"));
    auto *hide = window.findChild<QPushButton *>(QStringLiteral("hideSelectedButton"));
    auto *isolate = window.findChild<QPushButton *>(QStringLiteral("isolateSelectedButton"));
    auto *showAll = window.findChild<QPushButton *>(QStringLiteral("showAllButton"));
    auto *reset = window.findChild<QPushButton *>(QStringLiteral("fitAllButton"));
    auto *selector = window.findChild<QComboBox *>(QStringLiteral("equipmentSelector"));
    auto *scene = dynamic_cast<SceneView *>(window.findChild<QWidget *>(QStringLiteral("sceneView")));
    REQUIRE(list && search && id && material && hide && isolate && showAll && reset && selector && scene);
    REQUIRE(reset->text() == QStringLiteral("视角回正"));
    auto *canvas = scene->findChild<QWidget *>(QStringLiteral("sceneCanvas"));
    REQUIRE(canvas && list->count() == 3 && !controller.isIsolationActive());
    const QSet<QString> hideForA{bId, cId};
    const QSet<QString> hideForB{aId, cId};
    const QSet<QString> hideForC{aId, bId};

    REQUIRE(clickPart(list, aId));
    QTest::mouseClick(isolate, Qt::LeftButton);
    REQUIRE(controller.isIsolationActive() && controller.hiddenPartIds() == hideForA);
    REQUIRE(clickPart(list, bId));
    REQUIRE(controller.selectedPartId() == bId && id->text() == bId && material->text() == QStringLiteral("45钢"));
    // 1.1真人契约：隔离中有效单击即切换目标，清选中仍保隔离。
    REQUIRE(controller.isIsolationActive() && controller.hiddenPartIds() == hideForB);
    REQUIRE(capture(window, QStringLiteral("01-isolation-single-b-visible.png")));
    REQUIRE(clickPart(list, bId, true));
    REQUIRE(waitUntil([&] { return controller.isIsolationActive() && controller.hiddenPartIds() == hideForB; }));
    REQUIRE(controller.selectedPartId() == bId && id->text() == bId);
    REQUIRE(capture(window, QStringLiteral("02-double-b-only-b-visible.png")));
    for (int repeat = 0; repeat < 3; ++repeat) {
        REQUIRE(clickPart(list, bId, true));
        REQUIRE(controller.isIsolationActive() && controller.hiddenPartIds() == hideForB && controller.selectedPartId() == bId);
    }
    QTextStream(stdout) << "R07_WINDOW singleSwitchesIsolation=1 doubleKeepsExactId=1 repeatedDouble=1 sameNames=1" << Qt::endl;

    // 过滤清单不能退出隔离或改变可见性；选择被过滤时只有选择/属性清空。
    searchText(search, QStringLiteral("R07-C"));
    REQUIRE(list->count() == 1 && controller.selectedPartId().isEmpty() && id->text().isEmpty());
    REQUIRE(controller.isIsolationActive() && controller.hiddenPartIds() == hideForB);
    REQUIRE(clickPart(list, cId));
    REQUIRE(controller.hiddenPartIds() == hideForC && id->text() == cId);
    REQUIRE(clickPart(list, cId, true));
    REQUIRE(waitUntil([&] { return controller.hiddenPartIds() == hideForC; }));
    REQUIRE(controller.isIsolationActive() && controller.selectedPartId() == cId && search->text() == QStringLiteral("R07-C"));
    REQUIRE(capture(window, QStringLiteral("03-filtered-double-c.png")));
    searchText(search, QString());
    REQUIRE(list->count() == 3 && controller.hiddenPartIds() == hideForC);
    REQUIRE(!window.loadEquipment(QStringLiteral("missing-r07-device")));
    REQUIRE(controller.equipment()->id == equipmentId && controller.selectedPartId() == cId
            && controller.isIsolationActive() && controller.hiddenPartIds() == hideForC);
    QTest::mouseClick(showAll, Qt::LeftButton);
    REQUIRE(!controller.isIsolationActive() && controller.hiddenPartIds().isEmpty() && controller.selectedPartId() == cId);
    REQUIRE(clickPart(list, bId, true));
    REQUIRE(!controller.isIsolationActive() && controller.hiddenPartIds().isEmpty() && id->text() == bId);

    // 手工隐藏剩一件，仍不是显式隔离；双击不能根据剩余件数推断隔离状态。
    REQUIRE(clickPart(list, aId));
    QTest::mouseClick(hide, Qt::LeftButton);
    REQUIRE(clickPart(list, cId));
    QTest::mouseClick(hide, Qt::LeftButton);
    REQUIRE(controller.hiddenPartIds() == hideForB && !controller.isIsolationActive());
    REQUIRE(clickPart(list, cId, true));
    REQUIRE(controller.hiddenPartIds() == hideForB && !controller.isIsolationActive() && id->text() == cId);
    REQUIRE(capture(window, QStringLiteral("04-manual-hide-not-isolation.png")));
    QTest::mouseClick(showAll, Qt::LeftButton);
    REQUIRE(clickPart(list, bId));
    QTest::mouseClick(isolate, Qt::LeftButton);
    REQUIRE(controller.isIsolationActive() && controller.hiddenPartIds() == hideForB);

    // 方向和缩放各自实际变化后，回正恢复基准图；不更改选择/隐藏/隔离。
    auto *dimensions = window.findChild<QCheckBox *>(QStringLiteral("dimensionsCheck"));
    REQUIRE(dimensions && !dimensions->isChecked());
    dimensions->setFocus(); QTest::keyClick(dimensions, Qt::Key_Space); // 真键盘切换，不点击可能留白的控件中心。
    REQUIRE(dimensions->isChecked() && scene->dimensionsVisible());
    QTest::mouseClick(reset, Qt::LeftButton);
    QTest::qWait(80);
    const QImage baseline = scenePixels(*scene);
    REQUIRE(!baseline.isNull());
    const QPoint start = canvas->rect().center();
    const QPoint end = start + QPoint(70, 30);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
    QMouseEvent move(QEvent::MouseMove, QPointF(end), QPointF(canvas->mapToGlobal(end)),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
    QTest::qWait(80);
    const QImage rotated = scenePixels(*scene);
    REQUIRE(rotated != baseline);
    QWheelEvent wheel(QPointF(start), QPointF(canvas->mapToGlobal(start)), QPoint(), QPoint(0, 240),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(canvas, &wheel);
    QTest::qWait(80);
    REQUIRE(scenePixels(*scene) != rotated);
    REQUIRE(capture(window, QStringLiteral("05-camera-rotated-zoomed.png")));
    QTest::mouseClick(reset, Qt::LeftButton);
    QTest::qWait(80);
    REQUIRE(scenePixels(*scene) == baseline);
    REQUIRE(controller.selectedPartId() == bId && controller.isIsolationActive() && controller.hiddenPartIds() == hideForB);
    bool x = false;
    bool y = false;
    bool z = false;
    for (const auto &annotation : scene->measurementAnnotations()) {
        if (annotation.axis == QStringLiteral("X")) x = annotation.millimeters == 60;
        if (annotation.axis == QStringLiteral("Y")) y = annotation.millimeters == 20;
        if (annotation.axis == QStringLiteral("Z")) z = annotation.millimeters == 20;
    }
    REQUIRE(x && y && z);
    REQUIRE(capture(window, QStringLiteral("06-camera-reset-keeps-selection-visibility.png")));
    QTextStream(stdout) << "R07_WINDOW filteringKeepsIsolation=1 failedLoadPreserves=1 showAllExits=1 normalDoubleNoHide=1 manualOneVisibleNotIsolation=1 cameraResetDirectionAndZoom=1 statePreserved=1 selectedMeasurementMm=1" << Qt::endl;

    // 成功设备切换通过真实控件，清掉上一设备的隔离/显隐/选择。
    const int wanted = selector->findData(QStringLiteral("EQ-001"));
    REQUIRE(wanted >= 0);
    selector->setFocus();
    while (selector->currentIndex() != wanted) {
        const int before = selector->currentIndex();
        QTest::keyClick(selector, before < wanted ? Qt::Key_Down : Qt::Key_Up);
        QCoreApplication::processEvents();
        REQUIRE(selector->currentIndex() != before);
    }
    REQUIRE(controller.equipment()->id == QStringLiteral("EQ-001") && !controller.isIsolationActive()
            && controller.hiddenPartIds().isEmpty() && controller.selectedPartId().isEmpty() && id->text().isEmpty());
    REQUIRE(capture(window, QStringLiteral("07-device-switch-exits-isolation.png")));
    QTextStream(stdout) << "R07_WINDOW successfulSwitchExits=1 renderer='" << scene->rendererDescription()
                        << "' realUserFeedbackRequirement=1 automaticControls=1 acceptance=1" << Qt::endl;
    return true;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const int option = app.arguments().indexOf(QStringLiteral("--snapshot-dir"));
    if (option >= 0) {
        if (option + 1 >= app.arguments().size()) return 1;
        snapshots = QDir(app.arguments()[option + 1]).absolutePath();
        if (!QDir().mkpath(snapshots)) return 1;
    }
    return windowBehavior() ? 0 : 1;
}
