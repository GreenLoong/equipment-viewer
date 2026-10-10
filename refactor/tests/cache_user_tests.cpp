#include "apiclient.h"
#include "equipmentcontroller.h"
#include "equipmentwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <functional>
#include <memory>

namespace {
const QString equipmentId = QStringLiteral("缓存/设备 '工艺' ");
const QString partId = QStringLiteral("零件/支架 ");
const QString initialNote = QStringLiteral("初始缓存测试工艺意见。");
const QString latestNote = QStringLiteral("他人已更新：采用最新工艺卡。");
const QString userDraft = QStringLiteral("我的未提交草稿 'A'\n缓存断开也必须保留。");
QString snapshots;

#define REQUIRE(condition) do { if (!(condition)) { \
    QTextStream(stderr) << "CACHE GUI FAIL " << __LINE__ << ": " << #condition << Qt::endl; \
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

bool control(const QString &operation)
{
    const QDir directory(qEnvironmentVariable("EQUIPMENT_CACHE_CONTROL_DIR"));
    QFile request(directory.filePath(operation + QStringLiteral(".request")));
    REQUIRE(request.open(QIODevice::WriteOnly));
    REQUIRE(request.write("controlled-cache-gui") > 0);
    request.close();
    REQUIRE(waitUntil([&] { return QFile::exists(directory.filePath(operation + QStringLiteral(".done"))); }));
    return true;
}

bool capture(QWidget *widget, const QString &name)
{
    QCoreApplication::processEvents();
    return snapshots.isEmpty() || widget->grab().save(QDir(snapshots).filePath(name));
}

bool pasteDraft(QPlainTextEdit *draft, const QString &text)
{
    REQUIRE(draft->isEnabled());
    auto original = std::make_unique<QMimeData>();
    const auto *source = QApplication::clipboard()->mimeData();
    if (source) for (const auto &format : source->formats()) original->setData(format, source->data(format));
    QApplication::clipboard()->setText(text);
    QTest::mouseClick(draft, Qt::LeftButton);
    QTest::keyClick(draft, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(draft, Qt::Key_V, Qt::ControlModifier);
    QCoreApplication::processEvents();
    QApplication::clipboard()->setMimeData(original.release());
    REQUIRE(draft->toPlainText() == text);
    return true;
}

struct SharedUi {
    QPointer<QDialog> dialog;
    ApiClient *client = nullptr;
    QComboBox *equipment = nullptr;
    QPushButton *refresh = nullptr;
    QPushButton *download = nullptr;
    QComboBox *part = nullptr;
    QListWidget *notes = nullptr;
    QPlainTextEdit *saved = nullptr;
    QPlainTextEdit *draft = nullptr;
    QLabel *meta = nullptr;
    QLabel *status = nullptr;
    QPushButton *noteRefresh = nullptr;
    QPushButton *discard = nullptr;

    bool open(EquipmentWindow &window)
    {
        window.activateWindow();
        QTest::mouseClick(window.findChild<QPushButton *>(QStringLiteral("sharedLibraryButton")), Qt::LeftButton);
        REQUIRE(waitUntil([&] {
            for (auto *item : window.findChildren<QDialog *>()) {
                if (item->isVisible() && item->findChild<QLabel *>(QStringLiteral("sharedStatus"))) {
                    dialog = item;
                    return true;
                }
            }
            return false;
        }));
        client = nullptr;
        for (auto *item : dialog->findChildren<QObject *>()) if (auto *api = dynamic_cast<ApiClient *>(item)) client = api;
        equipment = dialog->findChild<QComboBox *>(QStringLiteral("sharedEquipment"));
        refresh = dialog->findChild<QPushButton *>(QStringLiteral("sharedRefreshButton"));
        download = dialog->findChild<QPushButton *>(QStringLiteral("sharedDownloadButton"));
        part = dialog->findChild<QComboBox *>(QStringLiteral("annotationPart"));
        notes = dialog->findChild<QListWidget *>(QStringLiteral("annotationList"));
        saved = dialog->findChild<QPlainTextEdit *>(QStringLiteral("annotationSaved"));
        draft = dialog->findChild<QPlainTextEdit *>(QStringLiteral("annotationDraft"));
        meta = dialog->findChild<QLabel *>(QStringLiteral("annotationMeta"));
        status = dialog->findChild<QLabel *>(QStringLiteral("annotationStatus"));
        noteRefresh = dialog->findChild<QPushButton *>(QStringLiteral("annotationRefreshButton"));
        discard = dialog->findChild<QPushButton *>(QStringLiteral("annotationDiscardButton"));
        REQUIRE(client && equipment && refresh && download && part && notes && saved && draft && meta && status && noteRefresh && discard);
        return true;
    }

    bool idle()
    {
        REQUIRE(waitUntil([&] { return dialog && client->pendingCount() == 0 && refresh->isEnabled(); }));
        return true;
    }

    bool click(QPushButton *button)
    {
        REQUIRE(button->isEnabled());
        QTest::mouseClick(button, Qt::LeftButton);
        return idle();
    }

    bool chooseEquipment(const QString &id)
    {
        const int wanted = equipment->findData(id);
        REQUIRE(wanted >= 0);
        equipment->setFocus();
        while (equipment->currentIndex() != wanted) {
            const int before = equipment->currentIndex();
            QTest::keyClick(equipment, before < wanted ? Qt::Key_Down : Qt::Key_Up);
            REQUIRE(idle() && equipment->currentIndex() != before);
        }
        REQUIRE(equipment->currentData().toString() == id);
        return true;
    }

    QJsonObject selectedNote() const
    {
        const auto selected = notes->selectedItems();
        return selected.isEmpty() ? QJsonObject()
            : QJsonDocument::fromJson(selected.first()->data(Qt::UserRole).toByteArray()).object();
    }

    bool selectNote(const QString &id)
    {
        for (int row = 0; row < notes->count(); ++row) {
            auto *item = notes->item(row);
            const auto dto = QJsonDocument::fromJson(item->data(Qt::UserRole).toByteArray()).object();
            if (dto.value(QStringLiteral("id")).toString() == id) {
                notes->scrollToItem(item);
                QTest::mouseClick(notes->viewport(), Qt::LeftButton, Qt::NoModifier, notes->visualItemRect(item).center());
                REQUIRE(selectedNote().value(QStringLiteral("id")).toString() == id);
                return true;
            }
        }
        return false;
    }

    bool close()
    {
        QTest::keyClick(dialog, Qt::Key_Escape);
        REQUIRE(waitUntil([&] { return dialog.isNull(); }));
        return true;
    }
};

bool runCacheUser()
{
    REQUIRE(!qEnvironmentVariable("EQUIPMENT_CACHE_CONTROL_DIR").isEmpty());
    const QString noteId = qEnvironmentVariable("EQUIPMENT_CACHE_NOTE_ID");
    REQUIRE(!noteId.isEmpty() && !qEnvironmentVariable("EQUIPMENT_SERVICE_CONFIG").isEmpty());
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(temporary.filePath(QStringLiteral("cache-client.sqlite"))) && repository.initialize());
    EquipmentController controller(repository);
    EquipmentWindow window(controller);
    REQUIRE(window.loadEquipment(QStringLiteral("EQ-001")));
    window.show();
    QTest::qWait(250);
    SharedUi ui;
    REQUIRE(ui.open(window) && ui.click(ui.refresh) && ui.chooseEquipment(equipmentId));
    REQUIRE(ui.click(ui.download));
    REQUIRE(controller.equipment()->id == equipmentId && controller.parts().size() == 2);
    REQUIRE(ui.close());
    auto *list = window.findChild<QListWidget *>(QStringLiteral("partsList"));
    REQUIRE(list);
    bool selected = false;
    for (int row = 0; row < list->count(); ++row) {
        auto *item = list->item(row);
        if (item->data(Qt::UserRole).toString() == partId) {
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(item).center());
            selected = true;
        }
    }
    REQUIRE(selected && controller.selectedPartId() == partId);
    REQUIRE(capture(&window, QStringLiteral("01-cache-downloaded-local.png")));
    REQUIRE(ui.open(window) && ui.click(ui.refresh) && ui.part->currentData().toString() == partId);
    REQUIRE(ui.selectNote(noteId) && ui.saved->toPlainText() == initialNote);
    REQUIRE(pasteDraft(ui.draft, userDraft));
    REQUIRE(ui.click(ui.refresh) && ui.draft->toPlainText() == userDraft && ui.saved->toPlainText() == initialNote);
    REQUIRE(capture(ui.dialog, QStringLiteral("02-warm-cache-draft.png")));
    REQUIRE(control(QStringLiteral("cache-down")));
    REQUIRE(ui.click(ui.refresh));
    REQUIRE(ui.draft->toPlainText() == userDraft && ui.saved->toPlainText() == initialNote
            && controller.equipment()->id == equipmentId && controller.selectedPartId() == partId);
    REQUIRE(control(QStringLiteral("cache-fault-check")));
    REQUIRE(capture(ui.dialog, QStringLiteral("03-cache-bypass-draft.png")));
    REQUIRE(control(QStringLiteral("peer-update")));
    REQUIRE(ui.click(ui.noteRefresh));
    REQUIRE(ui.saved->toPlainText() == latestNote && ui.draft->toPlainText() == userDraft
            && ui.selectedNote().value(QStringLiteral("revision")).toInt() == 2);
    REQUIRE(ui.meta->text().contains(QStringLiteral("UTC")) && ui.meta->text().contains(QStringLiteral("工艺乙")));
    REQUIRE(capture(ui.dialog, QStringLiteral("04-cache-down-latest-annotation.png")));
    REQUIRE(control(QStringLiteral("annotation-check")));
    REQUIRE(control(QStringLiteral("cache-up")));
    REQUIRE(ui.click(ui.refresh));
    REQUIRE(ui.saved->toPlainText() == latestNote && ui.draft->toPlainText() == userDraft
            && controller.equipment()->id == equipmentId && controller.selectedPartId() == partId);
    REQUIRE(capture(ui.dialog, QStringLiteral("05-cache-reconnected-draft.png")));
    REQUIRE(control(QStringLiteral("cache-recovery-check")));
    REQUIRE(ui.click(ui.discard) && ui.draft->toPlainText() == latestNote && ui.close());
    REQUIRE(controller.selectedPartId() == partId && capture(&window, QStringLiteral("06-cache-final-local-model.png")));
    // 新连接打开本次临时SQLite；GUI下载后的离线资料不是只存在于共享控件缓存。
    EquipmentRepository reopened;
    REQUIRE(reopened.openDatabase(repository.databasePath()) && reopened.initialize());
    QList<Part> persisted;
    REQUIRE(reopened.loadParts(equipmentId, persisted) && persisted.size() == 2);
    QTextStream(stdout) << "CACHE_GUI refresh=1 download=1 localPersistence=1 faultKeepsGeometryAndDraft=1 latestAnnotation=1 reconnect=1 discardClose=1 simulated=1" << Qt::endl;
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
    return runCacheUser() ? 0 : 1;
}
