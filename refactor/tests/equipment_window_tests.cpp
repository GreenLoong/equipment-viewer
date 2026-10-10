#include "equipmentcontroller.h"
#include "equipmentwindow.h"

#include <QApplication>
#include <QDir>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QUuid>

QString snapshotDirectory;
const QStringList propertyNames = {
    QStringLiteral("partId"), QStringLiteral("partName"), QStringLiteral("material"),
    QStringLiteral("shape"), QStringLiteral("positionX"), QStringLiteral("positionY"),
    QStringLiteral("positionZ"), QStringLiteral("sizeX"), QStringLiteral("sizeY"),
    QStringLiteral("sizeZ"), QStringLiteral("radius"), QStringLiteral("length")
};

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            QTextStream(stderr) << "FAIL at line " << __LINE__ << ": " << #condition << Qt::endl; \
            return false; \
        } \
    } while (false)

bool snapshot(EquipmentWindow &window, const QString &filename)
{
    if (snapshotDirectory.isEmpty())
        return true;
    QCoreApplication::processEvents();
    return window.grab().save(QDir(snapshotDirectory).filePath(filename));
}

bool clickPart(EquipmentWindow &window, const QString &id)
{
    auto *list = window.findChild<QListWidget *>(QStringLiteral("partsList"));
    REQUIRE(list);
    for (int row = 0; row < list->count(); ++row) {
        auto *item = list->item(row);
        if (item->data(Qt::UserRole).toString() == id) {
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                              list->visualItemRect(item).center());
            return true;
        }
    }
    return false;
}

bool propertiesMatch(EquipmentWindow &window, const QStringList &expected)
{
    REQUIRE(expected.size() == propertyNames.size());
    for (int index = 0; index < propertyNames.size(); ++index) {
        auto *label = window.findChild<QLabel *>(propertyNames.at(index));
        REQUIRE(label);
        REQUIRE(label->textFormat() == Qt::PlainText);
        REQUIRE(label->text() == expected.at(index));
    }
    return true;
}

bool removeTemporaryPartsTable(const QString &path)
{
    const QString name = QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool removed = false;
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        database.setDatabaseName(path);
        if (database.open()) {
            QSqlQuery query(database);
            removed = query.exec(QStringLiteral("DROP TABLE part"));
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(name);
    return removed;
}

bool controllerLoadsAndKeepsState()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(temporary.filePath(QStringLiteral("controller.sqlite"))));
    REQUIRE(repository.initialize());
    EquipmentController controller(repository);
    REQUIRE(controller.loadEquipment(QStringLiteral("EQ-001")));
    REQUIRE(controller.equipment()->name == QStringLiteral("输送机构"));
    REQUIRE(controller.parts().size() == 3);
    REQUIRE(controller.selectedPartId().isEmpty() && !controller.selectedPart());
    REQUIRE(controller.selectPart(QStringLiteral("P-003")));
    REQUIRE(controller.selectedPart()->name == QStringLiteral("传动轴"));
    REQUIRE(!controller.selectPart(QStringLiteral("不存在")));
    REQUIRE(controller.selectedPartId() == QStringLiteral("P-003"));
    REQUIRE(!controller.lastError().isEmpty());
    REQUIRE(!controller.loadEquipment(QStringLiteral("missing")));
    REQUIRE(controller.lastError().contains(QStringLiteral("不存在")));
    REQUIRE(controller.equipment()->id == QStringLiteral("EQ-001"));
    REQUIRE(controller.parts().size() == 3 && controller.selectedPartId() == QStringLiteral("P-003"));
    // 设备查询成功但零件查询失败时，也不能替换已显示的完整状态。
    REQUIRE(removeTemporaryPartsTable(repository.databasePath()));
    REQUIRE(!controller.loadEquipment(QStringLiteral("EQ-001")));
    REQUIRE(controller.lastError().contains(QStringLiteral("零件失败")));
    REQUIRE(controller.equipment()->id == QStringLiteral("EQ-001"));
    REQUIRE(controller.parts().size() == 3 && controller.selectedPartId() == QStringLiteral("P-003"));
    return true;
}

bool clicksUseCachedIds()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(temporary.filePath(QStringLiteral("clicks.sqlite"))));
    REQUIRE(repository.initialize());
    EquipmentController controller(repository);
    EquipmentWindow window(controller);
    REQUIRE(window.loadEquipment(QStringLiteral("EQ-001")));
    window.show();
    QCoreApplication::processEvents();
    auto *list = window.findChild<QListWidget *>(QStringLiteral("partsList"));
    auto *id = window.findChild<QLabel *>(QStringLiteral("partId"));
    auto *prompt = window.findChild<QLabel *>(QStringLiteral("selectionPrompt"));
    auto *title = window.findChild<QLabel *>(QStringLiteral("equipmentTitle"));
    auto *count = window.findChild<QLabel *>(QStringLiteral("partCount"));
    REQUIRE(list && id && prompt && title && count);
    REQUIRE(title->text() == QStringLiteral("输送机构"));
    REQUIRE(count->text() == QStringLiteral("3 个零件"));
    REQUIRE(list->count() == 3 && list->selectedItems().isEmpty());
    REQUIRE(prompt->isVisible() && prompt->text() == QStringLiteral("请选择零件"));
    REQUIRE(id->text().isEmpty());

    // 切换到未初始化的临时连接，后续点击仍必须使用原来的内存资料。
    REQUIRE(repository.openDatabase(temporary.filePath(QStringLiteral("uninitialized.sqlite"))));
    for (int row = 0; row < list->count(); ++row) {
        auto *item = list->item(row);
        const QString expected = item->data(Qt::UserRole).toString();
        REQUIRE(!expected.isEmpty());
        const QRect rectangle = list->visualItemRect(item);
        REQUIRE(!rectangle.isEmpty());
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, rectangle.center());
        REQUIRE(controller.selectedPartId() == expected);
        REQUIRE(controller.selectedPart() && controller.selectedPart()->id == expected);
        REQUIRE(id->isVisible() && id->text() == expected);
        REQUIRE(!prompt->isVisible());
    }
    REQUIRE(!window.loadEquipment(QStringLiteral("EQ-001")));
    REQUIRE(window.findChild<QLabel *>(QStringLiteral("equipmentStatus"))->text().contains(QStringLiteral("失败")));
    REQUIRE(list->count() == 3 && id->text() == QStringLiteral("P-003"));
    REQUIRE(list->selectedItems().size() == 1);
    REQUIRE(controller.selectedPartId() == QStringLiteral("P-003"));
    return true;
}

bool identicalNamesAndInvalidSelection()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(temporary.filePath(QStringLiteral("same-name.sqlite"))));
    REQUIRE(repository.initialize());
    QList<Part> parts;
    REQUIRE(repository.loadParts(QStringLiteral("EQ-001"), parts));
    for (Part &part : parts) {
        part.equipmentId = QStringLiteral("EQ-test");
        part.name = QStringLiteral("  同名零件 <b>原名</b>  ");
        part.material = QStringLiteral("材料 O'Hara <b>原文</b>\n下一行");
    }
    REQUIRE(repository.saveEquipmentWithParts({QStringLiteral("EQ-test"), QStringLiteral("<b>测试设备</b>")}, parts));
    EquipmentController controller(repository);
    EquipmentWindow window(controller);
    REQUIRE(window.loadEquipment(QStringLiteral("EQ-test")));
    window.show();
    QCoreApplication::processEvents();
    auto *list = window.findChild<QListWidget *>(QStringLiteral("partsList"));
    auto *id = window.findChild<QLabel *>(QStringLiteral("partId"));
    REQUIRE(window.findChild<QLabel *>(QStringLiteral("equipmentTitle"))->textFormat() == Qt::PlainText);
    for (int row = 0; row < 2; ++row) {
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                          list->visualItemRect(list->item(row)).center());
        REQUIRE(controller.selectedPart()->name == QStringLiteral("  同名零件 <b>原名</b>  "));
        REQUIRE(id->text() == parts.at(row).id);
        auto *name = window.findChild<QLabel *>(QStringLiteral("partName"));
        auto *material = window.findChild<QLabel *>(QStringLiteral("material"));
        REQUIRE(name && material);
        REQUIRE(name->text() == parts.at(row).name && name->textFormat() == Qt::PlainText);
        REQUIRE(material->text() == parts.at(row).material && material->textFormat() == Qt::PlainText);
    }
    const QString previous = id->text();
    list->item(2)->setData(Qt::UserRole, QStringLiteral("invalid"));
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(list->item(2)).center());
    REQUIRE(controller.selectedPartId() == previous && id->text() == previous);
    REQUIRE(list->selectedItems().size() == 1);
    REQUIRE(list->selectedItems().first()->data(Qt::UserRole).toString() == previous);
    REQUIRE(window.findChild<QLabel *>(QStringLiteral("equipmentStatus"))->text().contains(QStringLiteral("invalid")));
    return true;
}

bool emptyEquipmentIsDifferentFromFailure()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(temporary.filePath(QStringLiteral("empty.sqlite"))));
    REQUIRE(repository.initialize());
    REQUIRE(repository.saveEquipmentWithParts({QStringLiteral("EQ-empty"), QStringLiteral("空设备")}, {}));
    EquipmentController controller(repository);
    EquipmentWindow window(controller);
    REQUIRE(!window.loadEquipment(QStringLiteral("missing")));
    REQUIRE(!controller.equipment());
    REQUIRE(window.findChild<QLabel *>(QStringLiteral("partCount"))->text() == QStringLiteral("设备尚未加载"));
    REQUIRE(window.findChild<QLabel *>(QStringLiteral("equipmentStatus"))->text().contains(QStringLiteral("不存在")));
    REQUIRE(window.loadEquipment(QStringLiteral("EQ-empty")));
    REQUIRE(window.findChild<QLabel *>(QStringLiteral("partCount"))->text() == QStringLiteral("0 个零件"));
    REQUIRE(controller.equipment()->id == QStringLiteral("EQ-empty") && controller.parts().isEmpty());
    REQUIRE(!window.loadEquipment(QStringLiteral("missing")));
    REQUIRE(controller.equipment()->id == QStringLiteral("EQ-empty"));
    return true;
}

bool propertiesSwitchAndClear()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(temporary.filePath(QStringLiteral("properties.sqlite"))));
    REQUIRE(repository.initialize());
    EquipmentController controller(repository);
    EquipmentWindow window(controller);
    REQUIRE(window.loadEquipment(QStringLiteral("EQ-001")));
    window.show();
    QCoreApplication::processEvents();
    auto *list = window.findChild<QListWidget *>(QStringLiteral("partsList"));
    auto *clear = window.findChild<QPushButton *>(QStringLiteral("clearSelectionButton"));
    auto *prompt = window.findChild<QLabel *>(QStringLiteral("selectionPrompt"));
    auto *details = window.findChild<QWidget *>(QStringLiteral("propertyDetails"));
    auto *box = window.findChild<QGroupBox *>(QStringLiteral("boxDimensions"));
    auto *cylinder = window.findChild<QGroupBox *>(QStringLiteral("cylinderDimensions"));
    auto *position = window.findChild<QGroupBox *>(QStringLiteral("positionGroup"));
    REQUIRE(list && clear && prompt && details && box && cylinder && position);
    REQUIRE(position->title().contains(QStringLiteral("毫米")));
    REQUIRE(box->title().contains(QStringLiteral("毫米")) && cylinder->title().contains(QStringLiteral("毫米")));
    const QStringList emptyProperties(propertyNames.size(), QString());
    REQUIRE(propertiesMatch(window, emptyProperties));
    REQUIRE(clear->isEnabled());

    const QList<QStringList> expected = {
        {QStringLiteral("P-001"), QStringLiteral("底座"), QStringLiteral("Q235"), QStringLiteral("长方体"),
         QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("20"), QStringLiteral("600"),
         QStringLiteral("300"), QStringLiteral("40"), QString(), QString()},
        {QStringLiteral("P-003"), QStringLiteral("传动轴"), QStringLiteral("45钢"), QStringLiteral("圆柱"),
         QStringLiteral("120"), QStringLiteral("0"), QStringLiteral("120"), QString(), QString(), QString(),
         QStringLiteral("20"), QStringLiteral("400")},
        {QStringLiteral("P-002"), QStringLiteral("驱动电机"), QStringLiteral("铝合金"), QStringLiteral("长方体"),
         QStringLiteral("-150"), QStringLiteral("0"), QStringLiteral("120"), QStringLiteral("200"),
         QStringLiteral("160"), QStringLiteral("160"), QString(), QString()}
    };
    const QStringList files{QStringLiteral("01-base.png"), QStringLiteral("02-shaft.png"),
                            QStringLiteral("03-motor.png")};
    for (int step = 0; step < expected.size(); ++step) {
        REQUIRE(clickPart(window, expected.at(step).first()));
        REQUIRE(propertiesMatch(window, expected.at(step)));
        REQUIRE(controller.selectedPartId() == expected.at(step).first());
        REQUIRE(clear->isEnabled() && !prompt->isVisible() && details->isVisible());
        REQUIRE(box->isVisible() == (step != 1));
        REQUIRE(cylinder->isVisible() == (step == 1));
        REQUIRE(snapshot(window, files.at(step)));
    }

    QTest::mouseClick(clear, Qt::LeftButton);
    REQUIRE(list->selectedItems().isEmpty() && list->currentRow() == -1);
    REQUIRE(controller.selectedPartId().isEmpty() && !controller.selectedPart());
    REQUIRE(propertiesMatch(window, emptyProperties));
    REQUIRE(prompt->isVisible() && prompt->text() == QStringLiteral("请选择零件"));
    REQUIRE(!details->isVisible() && !box->isVisible() && !cylinder->isVisible());
    REQUIRE(clear->isEnabled());
    QTest::mouseClick(clear, Qt::LeftButton);
    REQUIRE(list->selectedItems().isEmpty() && list->currentRow() == -1);
    REQUIRE(controller.selectedPartId().isEmpty() && propertiesMatch(window, emptyProperties));
    REQUIRE(snapshot(window, QStringLiteral("04-cleared.png")));

    REQUIRE(clickPart(window, QStringLiteral("P-003")));
    REQUIRE(propertiesMatch(window, expected.at(1)));
    REQUIRE(controller.selectedPartId() == QStringLiteral("P-003"));
    REQUIRE(!box->isVisible() && cylinder->isVisible() && clear->isEnabled());
    REQUIRE(snapshot(window, QStringLiteral("05-shaft-again.png")));

    // 清单自身取消选择也必须清空全部属性，而非只响应专用按钮。
    list->clearSelection();
    REQUIRE(controller.selectedPartId().isEmpty() && !controller.selectedPart());
    REQUIRE(propertiesMatch(window, emptyProperties) && prompt->isVisible());

    REQUIRE(clickPart(window, QStringLiteral("P-001")));
    REQUIRE(!window.loadEquipment(QStringLiteral("missing")));
    REQUIRE(propertiesMatch(window, expected.at(0)) && controller.selectedPartId() == QStringLiteral("P-001"));
    // 成功重新加载后不保留任何旧借用指针或旧字段。
    REQUIRE(window.loadEquipment(QStringLiteral("EQ-001")));
    REQUIRE(controller.selectedPartId().isEmpty() && list->selectedItems().isEmpty());
    REQUIRE(propertiesMatch(window, emptyProperties) && prompt->isVisible());
    REQUIRE(clickPart(window, QStringLiteral("P-003")));
    REQUIRE(propertiesMatch(window, expected.at(1)));
    return true;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    const int snapshotOption = app.arguments().indexOf(QStringLiteral("--snapshot-dir"));
    if (snapshotOption >= 0) {
        if (snapshotOption + 1 >= app.arguments().size())
            return 1;
        snapshotDirectory = QDir(app.arguments().at(snapshotOption + 1)).absolutePath();
        if (!QDir().mkpath(snapshotDirectory))
            return 1;
    }
    const bool controller = controllerLoadsAndKeepsState();
    const bool clicks = clicksUseCachedIds();
    const bool identity = identicalNamesAndInvalidSelection();
    const bool empty = emptyEquipmentIsDifferentFromFailure();
    const bool properties = propertiesSwitchAndClear();
    QTextStream(stdout) << "Controller: " << controller << " CachedClicks: " << clicks
                        << " Identity: " << identity << " EmptyAndFailure: " << empty
                        << " PropertiesAndClear: " << properties << Qt::endl;
    return controller && clicks && identity && empty && properties ? 0 : 1;
}
