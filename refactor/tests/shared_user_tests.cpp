#include "apiclient.h"
#include "equipmentcontroller.h"
#include "equipmentwindow.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QClipboard>
#include <QDateTime>
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
    QComboBox *part = nullptr;
    QLineEdit *author = nullptr;
    QListWidget *notes = nullptr;
    QPlainTextEdit *saved = nullptr;
    QPlainTextEdit *draft = nullptr;
    QLabel *meta = nullptr;
    QLabel *noteStatus = nullptr;
    QPushButton *noteNew = nullptr;
    QPushButton *noteSave = nullptr;
    QPushButton *noteRefresh = nullptr;
    QPushButton *noteDiscard = nullptr;
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
        part = dialog->findChild<QComboBox *>(QStringLiteral("annotationPart"));
        author = dialog->findChild<QLineEdit *>(QStringLiteral("annotationAuthor"));
        notes = dialog->findChild<QListWidget *>(QStringLiteral("annotationList"));
        saved = dialog->findChild<QPlainTextEdit *>(QStringLiteral("annotationSaved"));
        draft = dialog->findChild<QPlainTextEdit *>(QStringLiteral("annotationDraft"));
        meta = dialog->findChild<QLabel *>(QStringLiteral("annotationMeta"));
        noteStatus = dialog->findChild<QLabel *>(QStringLiteral("annotationStatus"));
        noteNew = dialog->findChild<QPushButton *>(QStringLiteral("annotationNewButton"));
        noteSave = dialog->findChild<QPushButton *>(QStringLiteral("annotationSaveButton"));
        noteRefresh = dialog->findChild<QPushButton *>(QStringLiteral("annotationRefreshButton"));
        noteDiscard = dialog->findChild<QPushButton *>(QStringLiteral("annotationDiscardButton"));
        for (auto *child : dialog->findChildren<QObject *>()) {
            if (auto *api = dynamic_cast<ApiClient *>(child)) client = api;
        }
        REQUIRE(address && token && equipment && refresh && publish && download && status && client);
        REQUIRE(part && author && notes && saved && draft && meta && noteStatus
                && noteNew && noteSave && noteRefresh && noteDiscard && saved->isReadOnly());
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

    bool idle()
    {
        REQUIRE(waitUntil([&] { return dialog && client->pendingCount() == 0 && refresh->isEnabled(); }));
        return true;
    }

    bool chooseCombo(QComboBox *combo, const QString &id)
    {
        const int index = combo->findData(id);
        REQUIRE(index >= 0 && combo->isEnabled());
        if (combo->currentIndex() == index) return idle();
        combo->setFocus();
        // 每次真实键盘变更都会触发异步scope加载；逐项等完成，不把后续输入送给disabled控件。
        while (combo->currentIndex() != index) {
            const int before = combo->currentIndex();
            QTest::keyClick(combo, before < index ? Qt::Key_Down : Qt::Key_Up);
            REQUIRE(idle());
            REQUIRE(combo->currentIndex() != before);
        }
        if (combo->currentData().toString() != id) {
            QTextStream(stderr) << "COMBO_OBSERVATION name=" << combo->objectName()
                << " expected='" << id << "' actual='" << combo->currentData().toString()
                << "' index=" << combo->currentIndex() << " requested=" << index
                << " noteStatus='" << noteStatus->text() << "'" << Qt::endl;
            capture(dialog, QStringLiteral("failure-combo-selection.png"));
        }
        REQUIRE(combo->currentData().toString() == id);
        return true;
    }

    bool choose(const QString &id) { return chooseCombo(equipment, id); }

    bool closeWithEscape()
    {
        REQUIRE(dialog);
        token->setFocus();
        QTest::keyClick(token, Qt::Key_Escape);
        REQUIRE(waitUntil([&] { return dialog.isNull(); }));
        return true;
    }

    bool choosePart(const QString &id)
    {
        return chooseCombo(part, id);
    }

    QJsonObject selectedNote() const
    {
        const auto items = notes->selectedItems();
        return items.isEmpty() ? QJsonObject()
            : QJsonDocument::fromJson(items.first()->data(Qt::UserRole).toByteArray()).object();
    }

    bool selectNote(const QString &id)
    {
        for (int row = 0; row < notes->count(); ++row) {
            auto *item = notes->item(row);
            const auto value = QJsonDocument::fromJson(item->data(Qt::UserRole).toByteArray()).object();
            if (value.value(QStringLiteral("id")).toString() == id) {
                notes->scrollToItem(item);
                QTest::mouseClick(notes->viewport(), Qt::LeftButton, Qt::NoModifier,
                                  notes->visualItemRect(item).center());
                REQUIRE(selectedNote().value(QStringLiteral("id")).toString() == id);
                return true;
            }
        }
        return false;
    }
};

void typeAscii(QLineEdit *field, const QString &text)
{
    QTest::mouseClick(field, Qt::LeftButton);
    QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(field, text);
}

// 使用真实粘贴键输入中文/多行文本；保存并恢复用户剪贴板全部MIME格式。
bool pasteText(QWidget *field, const QString &text)
{
    REQUIRE(field && field->isEnabled());
    auto original = std::make_unique<QMimeData>();
    const QMimeData *source = QApplication::clipboard()->mimeData();
    if (source) for (const QString &format : source->formats()) original->setData(format, source->data(format));
    QApplication::clipboard()->setText(text);
    QTest::mouseClick(field, Qt::LeftButton);
    QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(field, Qt::Key_V, Qt::ControlModifier);
    QCoreApplication::processEvents();
    QApplication::clipboard()->setMimeData(original.release());
    return true;
}

bool signal(const QString &name, const QByteArray &content = QByteArray("requested"))
{
    QFile file(QDir(qEnvironmentVariable("EQUIPMENT_TEST_CONTROL_DIR")).filePath(name));
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(content) == content.size());
    file.close();
    return true;
}

bool marker(const QString &name)
{
    return QFile::exists(QDir(qEnvironmentVariable("EQUIPMENT_TEST_CONTROL_DIR")).filePath(name));
}

bool utcNote(SharedControls &controls, const QJsonObject &note)
{
    const QString updated = note.value(QStringLiteral("updatedAt")).toString();
    const QString created = note.value(QStringLiteral("createdAt")).toString();
    REQUIRE(updated.endsWith(QLatin1Char('Z')) && created.endsWith(QLatin1Char('Z')));
    REQUIRE(QDateTime::fromString(updated, Qt::ISODateWithMs).isValid());
    REQUIRE(controls.meta->text().contains(QStringLiteral("UTC")) && controls.meta->text().contains(updated));
    return true;
}

bool annotationScenarios(EquipmentWindow &windowA, EquipmentWindow &windowB,
                         EquipmentController &controllerA, EquipmentController &controllerB)
{
    const QString proxy = qEnvironmentVariable("EQUIPMENT_TEST_PROXY_URL");
    REQUIRE(!proxy.isEmpty());
    REQUIRE(windowA.loadEquipment(slashId) && windowB.loadEquipment(slashId));
    REQUIRE(selectPart(windowA) && selectPart(windowB));
    SharedControls a;
    SharedControls b;
    REQUIRE(a.open(windowA));
    typeAscii(a.address, proxy);
    REQUIRE(a.clickAndWait(a.refresh) && a.part->currentData().toString() == partId);
    REQUIRE(pasteText(a.author, QStringLiteral("工艺甲")));
    REQUIRE(a.clickAndWait(a.noteNew));
    const QString initial = QStringLiteral("复核装配间隙 'A'\n保留中文、多行和精确关联。");
    REQUIRE(pasteText(a.draft, initial) && a.draft->toPlainText() == initial);
    REQUIRE(a.clickAndWait(a.noteSave));
    const auto first = a.selectedNote();
    const QString noteId = first.value(QStringLiteral("id")).toString();
    REQUIRE(!noteId.isEmpty() && first.value(QStringLiteral("revision")).toInt() == 1);
    REQUIRE(first.value(QStringLiteral("equipmentId")).toString() == slashId
            && first.value(QStringLiteral("partId")).toString() == partId
            && first.value(QStringLiteral("author")).toString() == QStringLiteral("工艺甲"));
    REQUIRE(a.saved->toPlainText() == initial && utcNote(a, first));
    REQUIRE(capture(a.dialog, QStringLiteral("09-annotation-created-utc.png")));
    REQUIRE(b.open(windowB));
    typeAscii(b.address, proxy);
    REQUIRE(b.clickAndWait(b.refresh) && b.part->currentData().toString() == partId);
    REQUIRE(b.selectNote(noteId) && b.saved->toPlainText() == initial);
    REQUIRE(pasteText(b.author, QStringLiteral("工艺乙")));
    const QString aUpdate = QStringLiteral("甲已复核：间隙按工艺卡检查。");
    const QString bUpdate = QStringLiteral("乙的待提交意见\n需要保留，不静默覆盖甲。");
    REQUIRE(pasteText(a.draft, aUpdate) && pasteText(b.draft, bUpdate));

    // 真正用户关闭输入：草稿仍在时Esc与标准标题关闭事件都不能销毁窗口。
    b.draft->setFocus();
    QTest::keyClick(b.draft, Qt::Key_Escape);
    QCoreApplication::processEvents();
    REQUIRE(b.dialog && b.dialog->isVisible() && b.draft->toPlainText() == bUpdate);
    b.dialog->close();
    QCoreApplication::processEvents();
    REQUIRE(b.dialog && b.dialog->isVisible() && b.draft->toPlainText() == bUpdate
            && controllerB.selectedPartId() == partId);
    REQUIRE(capture(b.dialog, QStringLiteral("10-dirty-close-preserved.png")));
    REQUIRE(a.clickAndWait(a.noteSave) && a.selectedNote().value(QStringLiteral("revision")).toInt() == 2);
    REQUIRE(b.clickAndWait(b.noteSave));
    REQUIRE(marker(QStringLiteral("proxy.cas-conflict")));
    REQUIRE(b.noteStatus->text().contains(QStringLiteral("他人修改")) && b.noteStatus->text().contains(QStringLiteral("刷新")));
    REQUIRE(b.draft->toPlainText() == bUpdate && b.saved->toPlainText() == initial);
    REQUIRE(capture(b.dialog, QStringLiteral("11-cas-conflict-draft.png")));
    REQUIRE(b.clickAndWait(b.noteRefresh));
    REQUIRE(b.draft->toPlainText() == bUpdate && b.saved->toPlainText() == aUpdate
            && b.selectedNote().value(QStringLiteral("revision")).toInt() == 2);
    REQUIRE(capture(b.dialog, QStringLiteral("12-refreshed-separate-draft.png")));
    REQUIRE(b.clickAndWait(b.noteSave));
    const auto revision3 = b.selectedNote();
    REQUIRE(revision3.value(QStringLiteral("revision")).toInt() == 3 && b.saved->toPlainText() == bUpdate);
    REQUIRE(revision3.value(QStringLiteral("author")).toString() == QStringLiteral("工艺甲")
            && revision3.value(QStringLiteral("updatedBy")).toString() == QStringLiteral("工艺乙")
            && revision3.value(QStringLiteral("createdAt")) == first.value(QStringLiteral("createdAt")));
    REQUIRE(utcNote(b, revision3));
    QTextStream(stdout) << "ANNOTATION_USER createExactScope=1 multiline=1 utcVisible=1 casConflict=1 refreshKeepsDraft=1 explicitRevision3=1 dirtyEscapeAndTitleClose=1" << Qt::endl;

    // 设备与同名零件之间切换，精确过滤不会把另一范围的意见混进来。
    REQUIRE(b.choosePart(dotId) && b.notes->count() == 0);
    REQUIRE(b.choose(dotId) && b.choosePart(partId) && b.notes->count() == 0);
    REQUIRE(b.choose(slashId) && b.choosePart(partId) && b.selectNote(noteId));
    REQUIRE(b.choosePart(QString()) && b.clickAndWait(b.noteNew));
    const QString equipmentText = QStringLiteral("整台设备：装配前复核清单。");
    REQUIRE(pasteText(b.draft, equipmentText) && b.clickAndWait(b.noteSave));
    const auto equipmentNote = b.selectedNote();
    REQUIRE(equipmentNote.value(QStringLiteral("partId")).isNull());
    REQUIRE(b.choosePart(partId) && b.notes->count() == 1 && b.selectNote(noteId));
    REQUIRE(a.clickAndWait(a.noteRefresh));
    REQUIRE(a.saved->toPlainText() == bUpdate && a.selectedNote().value(QStringLiteral("revision")).toInt() == 3);
    QTextStream(stdout) << "ANNOTATION_USER sameNameAndCrossEquipmentExact=1 deviceNullScope=1 authorPreserved=1" << Qt::endl;

    // 已提交的201被代理丢掉；乙在真实服务上改动后，甲仍显式沿用原UUID重试。
    REQUIRE(a.clickAndWait(a.noteNew));
    const QString retryOriginal = QStringLiteral("幂等新增：复核定位销。");
    const QString retryLater = QStringLiteral("乙补充：定位销已经复核。");
    REQUIRE(pasteText(a.draft, retryOriginal) && signal(QStringLiteral("proxy.drop-create.arm")));
    REQUIRE(a.clickAndWait(a.noteSave));
    REQUIRE(marker(QStringLiteral("proxy.create-dropped")) && a.draft->toPlainText() == retryOriginal
            && a.saved->toPlainText().isEmpty());
    REQUIRE(capture(a.dialog, QStringLiteral("13-lost-create-response-draft.png")));
    REQUIRE(b.clickAndWait(b.noteRefresh));
    QString retryId;
    for (int row = 0; row < b.notes->count(); ++row) {
        const auto value = QJsonDocument::fromJson(b.notes->item(row)->data(Qt::UserRole).toByteArray()).object();
        if (value.value(QStringLiteral("text")).toString() == retryOriginal) retryId = value.value(QStringLiteral("id")).toString();
    }
    REQUIRE(!retryId.isEmpty() && b.selectNote(retryId));
    REQUIRE(pasteText(b.draft, retryLater) && b.clickAndWait(b.noteSave));
    REQUIRE(b.selectedNote().value(QStringLiteral("revision")).toInt() == 2);
    REQUIRE(a.clickAndWait(a.noteSave));
    REQUIRE(a.selectedNote().value(QStringLiteral("id")).toString() == retryId
            && a.selectedNote().value(QStringLiteral("revision")).toInt() == 2
            && a.saved->toPlainText() == retryLater && a.draft->toPlainText() == retryOriginal);
    REQUIRE(marker(QStringLiteral("proxy.idempotent-confirmed")));
    REQUIRE(capture(a.dialog, QStringLiteral("14-idempotent-current-revision-draft.png")));
    REQUIRE(a.clickAndWait(a.noteSave));
    REQUIRE(a.saved->toPlainText() == retryOriginal && a.selectedNote().value(QStringLiteral("revision")).toInt() == 3);
    QTextStream(stdout) << "ANNOTATION_USER committedResponseLost=1 sameUuidRetry=1 laterDtoKeepsDraft=1 explicitResubmit=1" << Qt::endl;

    const QString guardedDraft = QStringLiteral("保留身份保护草稿。");
    REQUIRE(pasteText(a.draft, guardedDraft));
    // dirty时选择另一个范围/批注会恢复原范围，草稿不能跟着同名对象迁移。
    a.part->setFocus();
    QTest::keyClick(a.part, Qt::Key_Home);
    REQUIRE(a.part->currentData().toString() == partId && a.draft->toPlainText() == guardedDraft);
    a.equipment->setFocus();
    QTest::keyClick(a.equipment, Qt::Key_Home);
    REQUIRE(a.equipment->currentData().toString() == slashId && a.draft->toPlainText() == guardedDraft);
    REQUIRE(signal(QStringLiteral("proxy.wrong-scope.arm")) && a.clickAndWait(a.noteRefresh));
    REQUIRE(a.noteStatus->text().contains(QStringLiteral("无效")) && a.draft->toPlainText() == guardedDraft
            && a.saved->toPlainText() == retryOriginal);
    REQUIRE(signal(QStringLiteral("proxy.missing-part.arm")) && a.clickAndWait(a.refresh));
    REQUIRE(a.part->currentData().toString() == partId && a.draft->toPlainText() == guardedDraft);
    REQUIRE(signal(QStringLiteral("proxy.wrong-identity.arm")) && a.clickAndWait(a.noteSave));
    REQUIRE(a.noteStatus->text().contains(QStringLiteral("不同批注")) && a.draft->toPlainText() == guardedDraft
            && a.saved->toPlainText() == retryOriginal);
    REQUIRE(capture(a.dialog, QStringLiteral("15-invalid-response-draft-preserved.png")));
    REQUIRE(a.clickAndWait(a.noteRefresh));
    REQUIRE(a.saved->toPlainText() == guardedDraft && a.selectedNote().value(QStringLiteral("revision")).toInt() == 4);

    const QString cancelledDraft = QStringLiteral("取消旧请求后仍需保留的草稿。");
    REQUIRE(pasteText(a.draft, cancelledDraft) && signal(QStringLiteral("proxy.delay-notes.arm")));
    QTest::mouseClick(a.noteRefresh, Qt::LeftButton);
    REQUIRE(waitUntil([&] { return marker(QStringLiteral("proxy.delay-notes.received")); }));
    REQUIRE(a.client->pendingCount() == 1);
    a.dialog->close();
    REQUIRE(a.dialog && a.dialog->isVisible() && a.draft->toPlainText() == cancelledDraft);
    // 立即发新请求，再让旧响应/取消事件到达；不能将新请求的busy或状态覆盖。
    REQUIRE(a.clickAndWait(a.noteRefresh));
    REQUIRE(waitUntil([&] { return marker(QStringLiteral("proxy.delay-notes.finished")); }));
    QTest::qWait(100);
    REQUIRE(a.noteRefresh->isEnabled() && a.draft->toPlainText() == cancelledDraft
            && a.saved->toPlainText() == guardedDraft
            && a.noteStatus->text().contains(QStringLiteral("草稿保留")));
    QTextStream(stdout) << "ANNOTATION_USER dirtyScopeProtected=1 wrongFilteredScopeRejected=1 missingPartProtected=1 wrongSaveIdentityRejected=1 oldCancelledCallbackIgnored=1" << Qt::endl;

    REQUIRE(signal(QStringLiteral("pause.request")) && waitUntil([&] { return marker(QStringLiteral("pause.done")); }));
    REQUIRE(a.clickAndWait(a.noteSave) && a.draft->toPlainText() == cancelledDraft
            && a.saved->toPlainText() == guardedDraft);
    REQUIRE(a.clickAndWait(a.noteRefresh) && a.draft->toPlainText() == cancelledDraft);
    REQUIRE(capture(a.dialog, QStringLiteral("16-service-down-annotation-draft.png")));
    REQUIRE(signal(QStringLiteral("resume.request")) && waitUntil([&] { return marker(QStringLiteral("resume.done")); }));
    REQUIRE(a.clickAndWait(a.noteRefresh) && a.draft->toPlainText() == cancelledDraft
            && a.saved->toPlainText() == guardedDraft && a.selectedNote().value(QStringLiteral("revision")).toInt() == 4);
    REQUIRE(a.clickAndWait(a.noteDiscard) && a.draft->toPlainText() == guardedDraft);
    REQUIRE(a.closeWithEscape() && controllerA.selectedPartId() == partId);
    REQUIRE(b.clickAndWait(b.noteRefresh) && b.selectNote(noteId));
    REQUIRE(b.saved->toPlainText() == bUpdate && b.selectedNote().value(QStringLiteral("revision")).toInt() == 3);
    REQUIRE(utcNote(b, b.selectedNote()) && capture(b.dialog, QStringLiteral("17-restarted-persisted-utc.png")));
    REQUIRE(b.closeWithEscape() && controllerB.selectedPartId() == partId);
    QTextStream(stdout) << "ANNOTATION_USER disconnectDraft=1 restartedPersistence=1 discardThenClose=1 noGeometryMutation=1 acceptance=1 simulated=1" << Qt::endl;
    return true;
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

    REQUIRE(annotationScenarios(windowA, windowB, controllerA, controllerB));
    REQUIRE(windowB.loadEquipment(localId) && selectPart(windowB));

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
