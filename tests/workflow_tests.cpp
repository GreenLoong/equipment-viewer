#include "equipmentcontroller.h"
#include "equipmentwindow.h"
#include "sceneview.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

#define REQUIRE(condition) do { if (!(condition)) { \
    QTextStream(stderr) << "FAIL " << __LINE__ << ": " << #condition << Qt::endl; return 1; } } while(false)

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(temporary.filePath(QStringLiteral("workflow.sqlite"))));
    REQUIRE(repository.initialize());
    EquipmentController controller(repository);
    EquipmentWindow window(controller);
    REQUIRE(window.loadEquipment(QStringLiteral("EQ-001")));
    window.show();
    app.processEvents();
    auto *list = window.findChild<QListWidget *>(QStringLiteral("partsList"));
    auto *search = window.findChild<QLineEdit *>(QStringLiteral("partSearch"));
    auto *id = window.findChild<QLabel *>(QStringLiteral("partId"));
    auto *hide = window.findChild<QPushButton *>(QStringLiteral("hideSelectedButton"));
    auto *isolate = window.findChild<QPushButton *>(QStringLiteral("isolateSelectedButton"));
    auto *show = window.findChild<QPushButton *>(QStringLiteral("showAllButton"));
    auto *scene = dynamic_cast<SceneView *>(window.findChild<QWidget *>(QStringLiteral("sceneView")));
    auto *selector = window.findChild<QComboBox *>(QStringLiteral("equipmentSelector"));
    REQUIRE(list && search && id && hide && isolate && show && scene && selector);
    REQUIRE(!hide->isEnabled() && !isolate->isEnabled());
    list->setCurrentRow(1);
    REQUIRE(controller.selectedPartId() == QStringLiteral("P-002"));
    QTest::mouseClick(hide, Qt::LeftButton);
    REQUIRE(controller.hiddenPartIds().contains(QStringLiteral("P-002")));
    REQUIRE(id->text() == QStringLiteral("P-002"));
    REQUIRE(list->currentItem()->text().contains(QStringLiteral("已隐藏")));
    list->setCurrentRow(0);
    list->setCurrentRow(1);
    REQUIRE(controller.hiddenPartIds().contains(QStringLiteral("P-002")));
    QTest::mouseClick(show, Qt::LeftButton);
    REQUIRE(controller.hiddenPartIds().isEmpty() && id->text() == QStringLiteral("P-002"));
    QTest::mouseClick(isolate, Qt::LeftButton);
    REQUIRE(controller.hiddenPartIds().size() == 2);
    REQUIRE(!controller.hiddenPartIds().contains(QStringLiteral("P-002")));
    // 过滤掉当前选择清属性，但不能改变模型可见性。
    search->setText(QStringLiteral("传动"));
    REQUIRE(list->count() == 1 && controller.selectedPartId().isEmpty());
    REQUIRE(id->text().isEmpty() && controller.hiddenPartIds().size() == 2);
    REQUIRE(!hide->isEnabled() && !isolate->isEnabled());
    list->setCurrentRow(0);
    REQUIRE(id->text() == QStringLiteral("P-003"));
    search->clear();
    REQUIRE(list->count() == 3 && id->text() == QStringLiteral("P-003"));
    QTest::mouseClick(show, Qt::LeftButton);
    REQUIRE(controller.hiddenPartIds().isEmpty());
    scene->onPartPicked(QStringLiteral("P-001"));
    REQUIRE(list->currentItem()->data(Qt::UserRole).toString() == QStringLiteral("P-001"));
    REQUIRE(id->text() == QStringLiteral("P-001"));
    const QString savedId = controller.equipment()->id;
    REQUIRE(!window.importData(QByteArray("{invalid")));
    REQUIRE(controller.equipment()->id == savedId && id->text() == QStringLiteral("P-001"));
    REQUIRE(!window.importData(QByteArray(5 * 1024 * 1024 + 1, ' ')));
    REQUIRE(controller.equipment()->id == savedId);
    REQUIRE(!window.importFile(temporary.path()));
    REQUIRE(controller.equipment()->id == savedId);
    const QStringList args = app.arguments();
    REQUIRE(args.size() >= 2);
    const QDir samples(args.last());
    QFile sample(samples.filePath(QStringLiteral("conveyor-learning.json")));
    REQUIRE(sample.open(QIODevice::ReadOnly));
    const QByteArray json = sample.readAll();
    REQUIRE(window.importData(json));
    REQUIRE(controller.equipment()->id == QStringLiteral("EQ-DEMO-101"));
    REQUIRE(controller.hiddenPartIds().isEmpty() && controller.selectedPartId().isEmpty());
    REQUIRE(selector->findData(QStringLiteral("EQ-DEMO-101")) >= 0);
    REQUIRE(!window.importData(json));
    REQUIRE(controller.equipment()->id == QStringLiteral("EQ-DEMO-101"));
    selector->setCurrentIndex(selector->findData(QStringLiteral("EQ-001")));
    REQUIRE(controller.equipment()->id == QStringLiteral("EQ-001"));
    REQUIRE(list->count() == 3 && controller.selectedPartId().isEmpty());
    // 重开独立连接，证明导入保存到磁盘，不仅存在于下拉框。
    EquipmentRepository reopened;
    REQUIRE(reopened.openDatabase(repository.databasePath()) && reopened.initialize());
    QList<Part> persisted;
    REQUIRE(reopened.loadParts(QStringLiteral("EQ-DEMO-101"), persisted));
    REQUIRE(!persisted.isEmpty());
    QTextStream(stdout) << "Workflow passed: visibility, isolation, search, scene selection, import protection, switching, persistence." << Qt::endl;
    return 0;
}
