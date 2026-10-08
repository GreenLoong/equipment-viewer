#include "apiclient.h"
#include "equipmentcontroller.h"
#include "equipmentwindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <functional>

namespace {
QString snapshots;
const QString slashId = QStringLiteral("共享/工艺 ");
const QString dotId = QStringLiteral("..");
const QString partId = QStringLiteral("零件/支架 ");

#define REQUIRE(condition) do { if (!(condition)) { \
    QTextStream(stderr) << "SHARED USER FAIL " << __LINE__ << ": " << #condition << Qt::endl; \
    return false; } } while (false)

bool waitUntil(const std::function<bool()> &condition, int timeoutMs = 12000)
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

QByteArray fixture(const QString &id)
{
    const QJsonArray parts{
        QJsonObject{{"id", partId}, {"name", QStringLiteral("同名支架")}, {"material", QStringLiteral("钢'材")},
                    {"shape", "box"}, {"dimensions", QJsonArray{100, 80, 60}}, {"position", QJsonArray{0, 0, 0}}},
        QJsonObject{{"id", dotId}, {"name", QStringLiteral("同名支架")}, {"material", QStringLiteral("铝")},
                    {"shape", "cylinder"}, {"dimensions", QJsonArray{10, 80}}, {"position", QJsonArray{160, 0, 0}}}
    };
    return QJsonDocument(QJsonObject{{"equipmentId", id}, {"equipmentName", QStringLiteral("模拟共享设备")},
                                    {"parts", parts}}).toJson(QJsonDocument::Compact);
}

bool capture(QWidget *widget, const QString &name)
{
    return snapshots.isEmpty() || widget->grab().save(QDir(snapshots).filePath(name));
}

bool selectPart(EquipmentWindow &window)
{
    auto *list = window.findChild<QListWidget *>(QStringLiteral("partsList"));
    REQUIRE(list);
    for (int row = 0; row < list->count(); ++row) {
        auto *item = list->item(row);
        if (item->data(Qt::UserRole).toString() == partId) {
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                              list->visualItemRect(item).center());
            return true;
        }
    }
    return false;
}

struct SharedControls {
    QPointer<QDialog> dialog;
    QLineEdit *address = nullptr;
    QLineEdit *token = nullptr;
    QComboBox *equipment = nullptr;
    QPushButton *refresh = nullptr;
    QPushButton *publish = nullptr;
    QPushButton *download = nullptr;
    QLabel *status = nullptr;
    ApiClient *client = nullptr;

    bool open(EquipmentWindow &window)
    {
        client = nullptr;
        window.show();
        window.activateWindow();
        auto *button = window.findChild<QPushButton *>(QStringLiteral("sharedLibraryButton"));
        REQUIRE(button);
        QTest::mouseClick(button, Qt::LeftButton);
        REQUIRE(waitUntil([&] {
            for (auto *child : window.findChildren<QDialog *>()) {
                if (child->isVisible() && child->findChild<QLabel *>(QStringLiteral("sharedStatus"))) {
                    dialog = child;
                    return true;
                }
            }
            return false;
        }));
        address = dialog->findChild<QLineEdit *>(QStringLiteral("sharedAddress"));
        token = dialog->findChild<QLineEdit *>(QStringLiteral("sharedToken"));
        equipment = dialog->findChild<QComboBox *>(QStringLiteral("sharedEquipment"));
        refresh = dialog->findChild<QPushButton *>(QStringLiteral("sharedRefreshButton"));
        publish = dialog->findChild<QPushButton *>(QStringLiteral("sharedPublishButton"));
        download = dialog->findChild<QPushButton *>(QStringLiteral("sharedDownloadButton"));
        status = dialog->findChild<QLabel *>(QStringLiteral("sharedStatus"));
        for (auto *child : dialog->findChildren<QObject *>()) {
            if (auto *api = dynamic_cast<ApiClient *>(child)) client = api;
        }
        REQUIRE(address && token && equipment && refresh && publish && download && status && client);
        REQUIRE(dialog->windowModality() == Qt::WindowModal);
        REQUIRE(token->echoMode() == QLineEdit::Password && !token->text().isEmpty());
        return true;
    }

    bool clickAndWait(QPushButton *button)
    {
        REQUIRE(button->isEnabled());
        QTest::mouseClick(button, Qt::LeftButton);
        REQUIRE(waitUntil([&] { return dialog && client->pendingCount() == 0 && refresh->isEnabled(); }));
        return true;
    }

    bool choose(const QString &id)
    {
        const int index = equipment->findData(id);
        REQUIRE(index >= 0);
        equipment->setFocus();
        QTest::keyClick(equipment, Qt::Key_Home);
        for (int row = 0; row < index; ++row) QTest::keyClick(equipment, Qt::Key_Down);
        REQUIRE(equipment->currentData().toString() == id);
        return true;
    }

    bool closeWithEscape()
    {
        REQUIRE(dialog);
        token->setFocus();
        QTest::keyClick(token, Qt::Key_Escape);
        REQUIRE(waitUntil([&] { return dialog.isNull(); }));
        return true;
    }
};

void typeAscii(QLineEdit *field, const QString &text)
{
    QTest::mouseClick(field, Qt::LeftButton);
    QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(field, text);
}

bool exactParts(const QList<Part> &parts, const QString &equipmentId)
{
    REQUIRE(parts.size() == 2);
    // SQLite显示顺序为ORDER BY id；共享验收核对身份与资料，不假定JSON数组顺序。
    const Part *box = nullptr;
    const Part *cylinder = nullptr;
    for (const Part &part : parts) {
        REQUIRE(part.equipmentId == equipmentId);
        if (part.id == partId) box = &part;
        if (part.id == dotId) cylinder = &part;
    }
    REQUIRE(box && cylinder);
    REQUIRE(box->name == QStringLiteral("同名支架") && cylinder->name == box->name);
    REQUIRE(box->material == QStringLiteral("钢'材") && cylinder->material == QStringLiteral("铝"));
    REQUIRE(box->shape == QStringLiteral("box") && box->sizeX == 100 && box->sizeY == 80 && box->sizeZ == 60
            && box->positionX == 0 && box->positionY == 0 && box->positionZ == 0
            && !box->radius.has_value() && !box->length.has_value());
    REQUIRE(cylinder->shape == QStringLiteral("cylinder") && cylinder->radius == 10 && cylinder->length == 80
            && cylinder->positionX == 160 && cylinder->positionY == 0 && cylinder->positionZ == 0
            && !cylinder->sizeX.has_value() && !cylinder->sizeY.has_value() && !cylinder->sizeZ.has_value());
    return true;
}

bool runScenarios()
{
    const QString controlRoot = qEnvironmentVariable("EQUIPMENT_TEST_CONTROL_DIR");
    const QString stallUrl = qEnvironmentVariable("EQUIPMENT_TEST_STALL_URL");
    REQUIRE(!controlRoot.isEmpty() && !stallUrl.isEmpty() && !qEnvironmentVariable("EQUIPMENT_SERVICE_CONFIG").isEmpty());
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    EquipmentRepository repositoryA;
    EquipmentRepository repositoryB;
    REQUIRE(repositoryA.openDatabase(temporary.filePath(QStringLiteral("client-a.sqlite"))) && repositoryA.initialize());
    const QString downloadedPath = temporary.filePath(QStringLiteral("client-b.sqlite"));
    REQUIRE(repositoryB.openDatabase(downloadedPath) && repositoryB.initialize());
    EquipmentController controllerA(repositoryA);
    EquipmentController controllerB(repositoryB);
    EquipmentWindow windowA(controllerA);
    EquipmentWindow windowB(controllerB);
    REQUIRE(windowA.importData(fixture(slashId)) && windowB.loadEquipment(QStringLiteral("EQ-001")));
    windowA.show();
    windowB.show();
    QTest::qWait(300);
    REQUIRE(selectPart(windowA) && controllerA.selectedPartId() == partId);
    SharedControls a;
    REQUIRE(a.open(windowA));
    REQUIRE(a.clickAndWait(a.publish));
    REQUIRE(a.status->text().contains(QStringLiteral("设备已发布")));
    REQUIRE(capture(a.dialog, QStringLiteral("01-client-a-published.png")));
    REQUIRE(a.clickAndWait(a.publish));
    REQUIRE(a.status->text().contains(QStringLiteral("已存在")));
    REQUIRE(controllerA.equipment()->id == slashId && controllerA.selectedPartId() == partId);
    REQUIRE(capture(a.dialog, QStringLiteral("02-duplicate-rejected.png")));
    REQUIRE(a.closeWithEscape());
    REQUIRE(controllerA.selectedPartId() == partId);
    QTextStream(stdout) << "SHARED_USER publish=1 duplicateProtected=1 modalEscapeParentPreserved=1" << Qt::endl;

    SharedControls b;
    REQUIRE(b.open(windowB) && b.clickAndWait(b.refresh));
    REQUIRE(b.status->text().contains(QStringLiteral("已连接共享库")) && b.equipment->count() == 1);
    REQUIRE(b.choose(slashId) && b.clickAndWait(b.download));
    REQUIRE(b.status->text().contains(QStringLiteral("已下载并保存")));
    REQUIRE(controllerB.equipment()->id == slashId && exactParts(controllerB.parts(), slashId));
    REQUIRE(capture(b.dialog, QStringLiteral("03-client-b-downloaded.png")));
    REQUIRE(b.clickAndWait(b.download));
    REQUIRE(b.status->text().contains(QStringLiteral("未覆盖本地")));
    REQUIRE(controllerB.equipment()->id == slashId && exactParts(controllerB.parts(), slashId));
    REQUIRE(b.closeWithEscape());
    REQUIRE(capture(&windowB, QStringLiteral("04-client-b-local.png")));
    QTextStream(stdout) << "SHARED_USER secondClientRefresh=1 slashChineseTrailingId=1 download=1 localDuplicateProtected=1" << Qt::endl;

    REQUIRE(windowA.importData(fixture(dotId)));
    REQUIRE(a.open(windowA) && a.clickAndWait(a.publish));
    REQUIRE(a.status->text().contains(QStringLiteral("设备已发布")));
    REQUIRE(a.closeWithEscape());
    REQUIRE(b.open(windowB) && b.clickAndWait(b.refresh) && b.equipment->count() == 2);
    REQUIRE(b.choose(dotId) && b.clickAndWait(b.download));
    REQUIRE(b.status->text().contains(QStringLiteral("已下载并保存")));
    REQUIRE(controllerB.equipment()->id == dotId && exactParts(controllerB.parts(), dotId));
    REQUIRE(b.closeWithEscape());
    REQUIRE(selectPart(windowB) && controllerB.selectedPartId() == partId);
    const QString localId = controllerB.equipment()->id;
    REQUIRE(b.open(windowB));
    typeAscii(b.token, QStringLiteral("deliberately-invalid-test-token"));
    REQUIRE(b.clickAndWait(b.refresh));
    REQUIRE(b.status->text().contains(QStringLiteral("令牌无效")));
    REQUIRE(controllerB.equipment()->id == localId && controllerB.selectedPartId() == partId);
    REQUIRE(capture(b.dialog, QStringLiteral("05-invalid-token.png")));
    REQUIRE(b.closeWithEscape());
    QTextStream(stdout) << "SHARED_USER dotId=1 invalidTokenPreservesLocal=1" << Qt::endl;

    // 独立挂起HTTP端口只用于用户在真实pending期间关闭；不调用业务回调制造取消。
    REQUIRE(b.open(windowB));
    typeAscii(b.address, stallUrl);
    QTest::mouseClick(b.refresh, Qt::LeftButton);
    REQUIRE(waitUntil([&] { return QFile::exists(QDir(controlRoot).filePath(QStringLiteral("stall.received"))); }));
    REQUIRE(b.client->pendingCount() == 1 && !b.refresh->isEnabled());
    REQUIRE(capture(b.dialog, QStringLiteral("06-pending-before-close.png")));
    REQUIRE(b.closeWithEscape());
    REQUIRE(controllerB.equipment()->id == localId && controllerB.selectedPartId() == partId);
    REQUIRE(waitUntil([&] { return QFile::exists(QDir(controlRoot).filePath(QStringLiteral("stall.closed"))); }));
    QTextStream(stdout) << "SHARED_USER pendingClose=1 realSocketClosed=1 modalEscapeParentPreserved=1" << Qt::endl;

    // 请求Node harness关闭本次ephemeral API，确认关闭完成后再用真实UI刷新。
    QFile stopRequest(QDir(controlRoot).filePath(QStringLiteral("stop.request")));
    REQUIRE(stopRequest.open(QIODevice::WriteOnly));
    stopRequest.write("close-owned-test-service");
    stopRequest.close();
    REQUIRE(waitUntil([&] { return QFile::exists(QDir(controlRoot).filePath(QStringLiteral("stop.done"))); }));
    REQUIRE(b.open(windowB) && b.clickAndWait(b.refresh));
    REQUIRE(!b.status->text().isEmpty() && !b.status->text().contains(QStringLiteral("已连接共享库")));
    REQUIRE(controllerB.equipment()->id == localId && controllerB.selectedPartId() == partId
            && exactParts(controllerB.parts(), localId));
    REQUIRE(capture(b.dialog, QStringLiteral("07-service-down-local-preserved.png")));
    REQUIRE(b.closeWithEscape());
    REQUIRE(controllerB.selectedPartId() == partId);
    EquipmentRepository offline;
    REQUIRE(offline.openDatabase(downloadedPath) && offline.initialize());
    QList<Part> persisted;
    REQUIRE(offline.loadParts(slashId, persisted) && exactParts(persisted, slashId));
    REQUIRE(offline.loadParts(dotId, persisted) && exactParts(persisted, dotId));
    EquipmentController offlineController(offline);
    EquipmentWindow offlineWindow(offlineController);
    REQUIRE(offlineWindow.loadEquipment(slashId));
    offlineWindow.show();
    QCoreApplication::processEvents();
    REQUIRE(selectPart(offlineWindow) && offlineController.selectedPartId() == partId);
    REQUIRE(offlineWindow.findChild<QLabel *>(QStringLiteral("partId"))->text() == partId);
    REQUIRE(capture(&offlineWindow, QStringLiteral("08-offline-reopened.png")));
    QTextStream(stdout) << "SHARED_USER realServiceStopped=1 disconnectPreservesLocal=1 reopenedOfflinePersistence=1" << Qt::endl;
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
    const bool passed = runScenarios();
    QTextStream(stdout) << "SHARED_USER acceptance=" << passed << " simulated=1" << Qt::endl;
    return passed ? 0 : 1;
}
