#include "equipmentcontroller.h"
#include "equipmentwindow.h"
#include "sceneview.h"

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

// 模拟工艺工程师的操作测试；不调用拾取回调或选择控制器来代替用户输入。
// --observe 记录当前版本表现；默认模式按0.3需求验收，未实现时应失败。
namespace {
const QString targetId = QStringLiteral("SU-TARGET");
QString snapshotDirectory;
bool observeOnly = false;

#define REQUIRE(condition) do { if (!(condition)) { \
    QTextStream(stderr) << "PRECONDITION FAIL " << __LINE__ << ": " << #condition << Qt::endl; \
    return false; } } while (false)

bool snapshot(EquipmentWindow &window, const QString &name)
{
    if (snapshotDirectory.isEmpty())
        return true;
    QCoreApplication::processEvents();
    return window.grab().save(QDir(snapshotDirectory).filePath(name));
}

bool clickTarget(QListWidget *list)
{
    for (int row = 0; row < list->count(); ++row) {
        auto *item = list->item(row);
        if (item->data(Qt::UserRole).toString() == targetId) {
            list->scrollToItem(item);
            QCoreApplication::processEvents();
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                              list->visualItemRect(item).center());
            QCoreApplication::processEvents();
            return true;
        }
    }
    return false;
}

bool runUserScenarios()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(temporary.filePath(QStringLiteral("simulated-user.sqlite"))));
    REQUIRE(repository.initialize());
    EquipmentController controller(repository);
    EquipmentWindow window(controller);
    // 一个居中模型使实际鼠标中心点击有确定目标；仅使用临时数据库的虚构资料。
    REQUIRE(window.importData(QByteArray(R"json({
        "equipmentId":"SU-EQUIPMENT","equipmentName":"Simulated user fixture",
        "parts":[{"id":"SU-TARGET","name":"target","material":"Q235","shape":"box",
                  "dimensions":[100,80,60],"position":[0,0,0]}]
    })json")));
    window.show();
    window.activateWindow();
    QTest::qWait(250);
    auto *list = window.findChild<QListWidget *>(QStringLiteral("partsList"));
    auto *search = window.findChild<QLineEdit *>(QStringLiteral("partSearch"));
    auto *id = window.findChild<QLabel *>(QStringLiteral("partId"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("equipmentStatus"));
    auto *prompt = window.findChild<QLabel *>(QStringLiteral("selectionPrompt"));
    auto *details = window.findChild<QWidget *>(QStringLiteral("propertyDetails"));
    auto *hide = window.findChild<QPushButton *>(QStringLiteral("hideSelectedButton"));
    auto *isolate = window.findChild<QPushButton *>(QStringLiteral("isolateSelectedButton"));
    auto *clear = window.findChild<QPushButton *>(QStringLiteral("clearSelectionButton"));
    auto *show = window.findChild<QPushButton *>(QStringLiteral("showAllButton"));
    auto *scene = dynamic_cast<SceneView *>(window.findChild<QWidget *>(QStringLiteral("sceneView")));
    REQUIRE(list && search && id && status && prompt && details && hide && isolate && clear && show && scene);
    auto *canvas = scene->findChild<QWidget *>(QStringLiteral("sceneCanvas"));
    REQUIRE(canvas);
    QTextStream(stdout) << "RENDERER " << scene->rendererDescription() << Qt::endl;

    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    REQUIRE(controller.selectedPartId() == targetId && id->text() == targetId);
    QTest::mouseClick(clear, Qt::LeftButton);
    REQUIRE(controller.selectedPartId().isEmpty());
    QTest::mouseClick(search, Qt::LeftButton);
    QTest::keyClicks(search, "not-found");
    REQUIRE(search->text() == QStringLiteral("not-found") && list->count() == 0);
    REQUIRE(controller.hiddenPartIds().isEmpty());
    REQUIRE(snapshot(window, QStringLiteral("01-filter-before-pick.png")));
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    QCoreApplication::processEvents();
    const bool filteredPickAccepted = controller.selectedPartId() == targetId
        && id->text() == targetId && search->text().isEmpty() && list->selectedItems().size() == 1
        && list->selectedItems().first()->data(Qt::UserRole).toString() == targetId
        && controller.hiddenPartIds().isEmpty();
    QTextStream(stdout) << "SEARCH_FILTERED_CLICK accepted=" << filteredPickAccepted
        << " selected='" << controller.selectedPartId() << "' search='" << search->text()
        << "' rows=" << list->count() << " status='" << status->text() << "'" << Qt::endl;
    REQUIRE(snapshot(window, QStringLiteral("02-filter-after-pick.png")));

    QTest::mouseClick(search, Qt::LeftButton);
    QTest::keyClick(search, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(search, "target");
    REQUIRE(list->count() == 1);
    bool escapesAccepted = true;
    const QList<QPair<QString, QWidget *>> focuses{
        {QStringLiteral("list"), list}, {QStringLiteral("canvas"), canvas},
        {QStringLiteral("search"), search}, {QStringLiteral("button"), show}
    };
    for (const auto &focus : focuses) {
        REQUIRE(clickTarget(list));
        REQUIRE(controller.selectedPartId() == targetId && id->text() == targetId);
        // 选中后隐藏再按Escape；快捷键必须只清选择，不顺带恢复显隐或清空搜索。
        QTest::mouseClick(hide, Qt::LeftButton);
        REQUIRE(controller.hiddenPartIds().contains(targetId));
        focus.second->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        REQUIRE(focus.second->hasFocus());
        QTest::keyClick(focus.second, Qt::Key_Escape);
        QCoreApplication::processEvents();
        const bool accepted = controller.selectedPartId().isEmpty() && id->text().isEmpty()
            && list->selectedItems().isEmpty() && list->currentRow() == -1 && prompt->isVisible()
            && !details->isVisible() && !hide->isEnabled() && !isolate->isEnabled()
            && search->text() == QStringLiteral("target") && controller.hiddenPartIds().contains(targetId);
        escapesAccepted = escapesAccepted && accepted;
        QTextStream(stdout) << "ESCAPE focus=" << focus.first << " accepted=" << accepted
            << " selected='" << controller.selectedPartId() << "' search='" << search->text()
            << "' hidden=" << controller.hiddenPartIds().size() << Qt::endl;
        REQUIRE(snapshot(window, QStringLiteral("03-escape-%1.png").arg(focus.first)));
        // 用既有按钮恢复场景，使每个焦点场景有相同的起始状态。
        QTest::mouseClick(clear, Qt::LeftButton);
        QTest::mouseClick(show, Qt::LeftButton);
    }
    QTextStream(stdout) << "SIMULATED_USER observed=1 filteredPick=" << filteredPickAccepted
        << " escape=" << escapesAccepted << " mode=" << (observeOnly ? "observe" : "accept") << Qt::endl;
    return observeOnly || (filteredPickAccepted && escapesAccepted);
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    observeOnly = app.arguments().contains(QStringLiteral("--observe"));
    const int snapshotOption = app.arguments().indexOf(QStringLiteral("--snapshot-dir"));
    if (snapshotOption >= 0) {
        if (snapshotOption + 1 >= app.arguments().size())
            return 1;
        snapshotDirectory = QDir(app.arguments().at(snapshotOption + 1)).absolutePath();
        if (!QDir().mkpath(snapshotDirectory))
            return 1;
    }
    return runUserScenarios() ? 0 : 1;
}
