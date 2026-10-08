#include "collaborationdialog.h"
#include "apiclient.h"
#include "equipmentcontroller.h"
#include "equipmentimporter.h"

#include <QComboBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDir>
#include <QDateTime>
#include <QGroupBox>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSplitter>
#include <QUuid>
#include <QRegularExpression>
#include <cmath>
#include <QCloseEvent>

namespace {
QString encoded(const QString &value) { return QString::fromLatin1(QUrl::toPercentEncoding(value)); }

bool validNote(const QJsonObject &note, const QString &equipmentId)
{
    static const QRegularExpression uuid(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    const auto revision = note.value(QStringLiteral("revision"));
    const auto part = note.value(QStringLiteral("partId"));
    return uuid.match(note.value(QStringLiteral("id")).toString()).hasMatch()
        && note.value(QStringLiteral("equipmentId")).toString() == equipmentId
        && (part.isNull() || (part.isString() && !part.toString().trimmed().isEmpty() && part.toString().size() <= 128))
        && note.value(QStringLiteral("text")).isString() && !note.value(QStringLiteral("text")).toString().trimmed().isEmpty()
        && note.value(QStringLiteral("text")).toString().size() <= 2000
        && note.value(QStringLiteral("author")).isString() && !note.value(QStringLiteral("author")).toString().trimmed().isEmpty()
        && note.value(QStringLiteral("author")).toString().size() <= 80
        && note.value(QStringLiteral("updatedBy")).isString() && !note.value(QStringLiteral("updatedBy")).toString().trimmed().isEmpty()
        && note.value(QStringLiteral("updatedBy")).toString().size() <= 80
        && revision.isDouble() && revision.toDouble() >= 1 && revision.toDouble() <= 2147483647
        && std::floor(revision.toDouble()) == revision.toDouble()
        && QDateTime::fromString(note.value(QStringLiteral("createdAt")).toString(), Qt::ISODateWithMs).isValid()
        && QDateTime::fromString(note.value(QStringLiteral("updatedAt")).toString(), Qt::ISODateWithMs).isValid();
}

QJsonObject deviceDocument(const Equipment &equipment, const QList<Part> &parts)
{
    QJsonArray items;
    for (const Part &part : parts) {
        const QJsonArray dimensions = part.shape == QStringLiteral("box")
            ? QJsonArray{part.sizeX.value_or(0), part.sizeY.value_or(0), part.sizeZ.value_or(0)}
            : QJsonArray{part.radius.value_or(0), part.length.value_or(0)};
        items.append(QJsonObject{{"id", part.id}, {"name", part.name}, {"material", part.material},
            {"shape", part.shape}, {"dimensions", dimensions},
            {"position", QJsonArray{part.positionX, part.positionY, part.positionZ}}});
    }
    return {{"equipmentId", equipment.id}, {"equipmentName", equipment.name}, {"parts", items}};
}
}

CollaborationDialog::CollaborationDialog(EquipmentController &controller, QWidget *parent)
    : QDialog(parent), controller_(controller), client_(new ApiClient(this)),
      address_(new QLineEdit(QStringLiteral("http://127.0.0.1:8765"), this)),
      token_(new QLineEdit(this)), equipment_(new QComboBox(this)),
      refresh_(new QPushButton(QStringLiteral("刷新共享库"), this)),
      publish_(new QPushButton(QStringLiteral("发布当前设备"), this)),
      download_(new QPushButton(QStringLiteral("下载到本地"), this)), status_(new QLabel(this))
{
    setWindowTitle(QStringLiteral("团队共享设备库"));
    setWindowModality(Qt::WindowModal);
    resize(850, 760);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    address_->setObjectName(QStringLiteral("sharedAddress"));
    token_->setObjectName(QStringLiteral("sharedToken"));
    token_->setEchoMode(QLineEdit::Password);
    equipment_->setObjectName(QStringLiteral("sharedEquipment"));
    form->addRow(QStringLiteral("服务地址"), address_);
    form->addRow(QStringLiteral("访问令牌"), token_);
    form->addRow(QStringLiteral("共享设备"), equipment_);
    layout->addLayout(form);
    auto *actions = new QHBoxLayout;
    refresh_->setObjectName(QStringLiteral("sharedRefreshButton"));
    publish_->setObjectName(QStringLiteral("sharedPublishButton"));
    download_->setObjectName(QStringLiteral("sharedDownloadButton"));
    actions->addWidget(refresh_); actions->addWidget(publish_); actions->addWidget(download_);
    layout->addLayout(actions);
    auto *hint = new QLabel(QStringLiteral("共享资料保存于团队服务；下载后可离线查看。本地同ID资料不会被覆盖。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    status_->setObjectName(QStringLiteral("sharedStatus"));
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto *noteGroup = new QGroupBox(QStringLiteral("工艺批注（最近 200 条）"), this);
    auto *noteLayout = new QVBoxLayout(noteGroup);
    auto *noteForm = new QFormLayout;
    partScope_ = new QComboBox(noteGroup);
    partScope_->setObjectName(QStringLiteral("annotationPart"));
    partScope_->addItem(QStringLiteral("整台设备（所有批注）"), QString());
    author_ = new QLineEdit(QStringLiteral("工艺工程师"), noteGroup);
    author_->setObjectName(QStringLiteral("annotationAuthor"));
    author_->setMaxLength(80);
    noteForm->addRow(QStringLiteral("关联零件"), partScope_);
    noteForm->addRow(QStringLiteral("我的显示名"), author_);
    noteLayout->addLayout(noteForm);
    auto *splitter = new QSplitter(Qt::Horizontal, noteGroup);
    notes_ = new QListWidget(splitter);
    notes_->setObjectName(QStringLiteral("annotationList"));
    auto *editPane = new QWidget(splitter);
    auto *editLayout = new QVBoxLayout(editPane);
    noteMeta_ = new QLabel(QStringLiteral("请选择批注或新建"), editPane);
    noteMeta_->setObjectName(QStringLiteral("annotationMeta"));
    noteMeta_->setTextFormat(Qt::PlainText);
    noteMeta_->setWordWrap(true);
    saved_ = new QPlainTextEdit(editPane);
    saved_->setObjectName(QStringLiteral("annotationSaved"));
    saved_->setReadOnly(true);
    saved_->setMaximumHeight(110);
    draft_ = new QPlainTextEdit(editPane);
    draft_->setObjectName(QStringLiteral("annotationDraft"));
    draft_->setPlaceholderText(QStringLiteral("输入工艺问题或装配说明，最多2000个字符"));
    editLayout->addWidget(noteMeta_);
    editLayout->addWidget(new QLabel(QStringLiteral("最新已保存内容"), editPane));
    editLayout->addWidget(saved_);
    editLayout->addWidget(new QLabel(QStringLiteral("我的草稿"), editPane));
    editLayout->addWidget(draft_, 1);
    splitter->setStretchFactor(1, 2);
    noteLayout->addWidget(splitter, 1);
    auto *noteActions = new QHBoxLayout;
    noteNew_ = new QPushButton(QStringLiteral("新批注"), noteGroup);
    noteNew_->setObjectName(QStringLiteral("annotationNewButton"));
    noteSave_ = new QPushButton(QStringLiteral("新增批注"), noteGroup);
    noteSave_->setObjectName(QStringLiteral("annotationSaveButton"));
    noteRefresh_ = new QPushButton(QStringLiteral("刷新批注"), noteGroup);
    noteRefresh_->setObjectName(QStringLiteral("annotationRefreshButton"));
    noteDiscard_ = new QPushButton(QStringLiteral("放弃草稿"), noteGroup);
    noteDiscard_->setObjectName(QStringLiteral("annotationDiscardButton"));
    for (auto *button : {noteNew_, noteSave_, noteRefresh_, noteDiscard_}) noteActions->addWidget(button);
    noteLayout->addLayout(noteActions);
    noteStatus_ = new QLabel(noteGroup);
    noteStatus_->setObjectName(QStringLiteral("annotationStatus"));
    noteStatus_->setTextFormat(Qt::PlainText);
    noteStatus_->setWordWrap(true);
    noteLayout->addWidget(noteStatus_);
    auto *draftHint = new QLabel(QStringLiteral("有未提交草稿时，先保存或放弃再关闭；断线不会自动重发。"), noteGroup);
    draftHint->setWordWrap(true);
    noteLayout->addWidget(draftHint);
    layout->addWidget(noteGroup, 1);
    auto *close = new QPushButton(QStringLiteral("关闭"), this);
    layout->addWidget(close);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(this, &QDialog::finished, this, [this] { client_->cancelAll(); });
    connect(refresh_, &QPushButton::clicked, this, &CollaborationDialog::refresh);
    connect(publish_, &QPushButton::clicked, this, &CollaborationDialog::publish);
    connect(download_, &QPushButton::clicked, this, &CollaborationDialog::download);
    connect(equipment_, &QComboBox::currentIndexChanged, this, [this] { download_->setEnabled(equipment_->count() > 0); });
    connect(equipment_, &QComboBox::currentIndexChanged, this, [this] {
        if (busy_) return;
        if (dirty_) {
            const QSignalBlocker blocker(equipment_);
            equipment_->setCurrentIndex(equipment_->findData(loadedEquipmentId_));
            noteStatus(QStringLiteral("草稿未保存，请先提交或放弃草稿再切换设备。"), true);
            return;
        }
        loadPartScope();
    });
    connect(partScope_, &QComboBox::currentIndexChanged, this, [this] {
        if (busy_) return;
        if (dirty_) {
            const QSignalBlocker blocker(partScope_);
            partScope_->setCurrentIndex(partScope_->findData(loadedPartId_));
            noteStatus(QStringLiteral("草稿未保存，请先提交或放弃后切换零件。"), true);
            return;
        }
        loadedPartId_ = partScope_->currentData().toString();
        displayNote({}, false);
        refreshNotes();
    });
    connect(notes_, &QListWidget::itemSelectionChanged, this, &CollaborationDialog::selectNote);
    connect(draft_, &QPlainTextEdit::textChanged, this, [this] {
        dirty_ = draft_->toPlainText() != currentNote_.value(QStringLiteral("text")).toString();
        setBusy(busy_);
    });
    connect(noteNew_, &QPushButton::clicked, this, &CollaborationDialog::newNote);
    connect(noteSave_, &QPushButton::clicked, this, &CollaborationDialog::saveNote);
    connect(noteRefresh_, &QPushButton::clicked, this, &CollaborationDialog::refreshNotes);
    connect(noteDiscard_, &QPushButton::clicked, this, &CollaborationDialog::discardDraft);
    const QString config = qEnvironmentVariable("EQUIPMENT_SERVICE_CONFIG",
        QDir::current().filePath(QStringLiteral("api/runtime/client.local.json")));
    QFile file(config);
    if (file.open(QIODevice::ReadOnly)) {
        const auto settings = QJsonDocument::fromJson(file.read(16384)).object();
        const QString host = settings.value(QStringLiteral("host")).toString(QStringLiteral("127.0.0.1"));
        const int port = settings.value(QStringLiteral("port")).toInt(8765);
        address_->setText(QStringLiteral("http://%1:%2").arg(host).arg(port));
        token_->setText(settings.value(QStringLiteral("token")).toString());
    }
    setBusy(false);
}

bool CollaborationDialog::configure()
{
    if (client_->configure(QUrl(address_->text().trimmed()), token_->text()))
        return true;
    status(QStringLiteral("请输入有效HTTP(S)地址和访问令牌。"), true);
    return false;
}

void CollaborationDialog::request(const QByteArray &method, const QString &path, const QJsonValue &body,
                                  std::function<void(ApiResult)> callback)
{
    const quint64 operation = ++operation_;
    client_->request(method, path, body, [this, operation, callback = std::move(callback)](ApiResult result) mutable {
        if (operation == operation_) callback(std::move(result));
    });
}

void CollaborationDialog::reject()
{
    if (dirty_) {
        ++operation_;
        client_->cancelAll();
        setBusy(false);
        noteStatus(QStringLiteral("草稿未提交，窗口已保留。先保存或放弃再关闭；远端可能已提交，请刷新确认。"), true);
        return;
    }
    QDialog::reject();
}

void CollaborationDialog::closeEvent(QCloseEvent *event)
{
    if (dirty_) { reject(); event->ignore(); }
    else QDialog::closeEvent(event);
}

void CollaborationDialog::setBusy(bool busy)
{
    busy_ = busy;
    address_->setEnabled(!busy && !dirty_); token_->setEnabled(!busy && !dirty_); equipment_->setEnabled(!busy);
    refresh_->setEnabled(!busy);
    publish_->setEnabled(!busy && controller_.equipment().has_value());
    download_->setEnabled(!busy && equipment_->count() > 0);
    const bool scope = hasScope();
    partScope_->setEnabled(!busy && scope);
    author_->setEnabled(!busy && scope);
    notes_->setEnabled(!busy && scope);
    draft_->setEnabled(!busy && scope);
    noteNew_->setEnabled(!busy && scope);
    noteRefresh_->setEnabled(!busy && scope);
    noteDiscard_->setEnabled(!busy && scope && dirty_);
    noteSave_->setEnabled(!busy && scope && dirty_);
    noteSave_->setText(currentNote_.isEmpty() ? QStringLiteral("新增批注")
        : QStringLiteral("提交修订（版本 %1）").arg(currentNote_.value(QStringLiteral("revision")).toInt()));
}

void CollaborationDialog::status(const QString &text, bool error)
{
    status_->setText(text);
    status_->setStyleSheet(error ? QStringLiteral("color:#b42318;") : QString());
}

void CollaborationDialog::refresh()
{
    if (!configure()) return;
    setBusy(true);
    status(QStringLiteral("正在读取共享库…"));
    request("GET", QStringLiteral("/api/equipment"), {}, [this](ApiResult result) {
        if (result.ok() && result.data.isArray()) {
            if (dirty_) {
                bool currentPresent = false;
                for (const auto &value : result.data.toArray())
                    currentPresent = currentPresent || value.toObject().value(QStringLiteral("id")).toString() == loadedEquipmentId_;
                if (!currentPresent) {
                    status(QStringLiteral("共享列表未包含当前设备，原草稿与范围已保留。"), true);
                    setBusy(false); return;
                }
            }
            const QSignalBlocker blocker(equipment_);
            QString wanted = loadedEquipmentId_;
            if (wanted.isEmpty() && controller_.equipment()) wanted = controller_.equipment()->id;
            equipment_->clear();
            for (const auto &item : result.data.toArray()) {
                const auto device = item.toObject();
                const QString id = device.value(QStringLiteral("id")).toString();
                equipment_->addItem(QStringLiteral("%1 (%2)").arg(device.value(QStringLiteral("name")).toString(), id), id);
            }
            const int index = equipment_->findData(wanted);
            equipment_->setCurrentIndex(index >= 0 ? index : (equipment_->count() ? 0 : -1));
            status(QStringLiteral("已连接共享库，共 %1 台设备。" ).arg(equipment_->count()));
        } else
            status(result.error.isEmpty() ? QStringLiteral("共享库返回了无效设备列表。") : result.error, true);
        setBusy(false);
        if (result.ok() && result.data.isArray() && equipment_->count() > 0) loadPartScope();
    });
}

bool CollaborationDialog::hasScope() const
{
    return !loadedEquipmentId_.isEmpty() && loadedEquipmentId_ == equipment_->currentData().toString();
}

void CollaborationDialog::noteStatus(const QString &text, bool error)
{
    noteStatus_->setText(text);
    noteStatus_->setStyleSheet(error ? QStringLiteral("color:#b42318;") : QString());
}

void CollaborationDialog::loadPartScope()
{
    if (equipment_->currentIndex() < 0 || !configure()) return;
    const QString id = equipment_->currentData().toString();
    setBusy(true);
    request("GET", QStringLiteral("/api/equipment?id=") + encoded(id), {}, [this, id](ApiResult result) {
        Equipment parsed;
        QList<Part> parts;
        QString error;
        const QByteArray json = result.data.isObject() ? QJsonDocument(result.data.toObject()).toJson(QJsonDocument::Compact) : QByteArray();
        if (!result.ok() || !EquipmentImporter::parse(json, parsed, parts, error) || parsed.id != id) {
            if (!loadedEquipmentId_.isEmpty()) {
                const QSignalBlocker blocker(equipment_);
                equipment_->setCurrentIndex(equipment_->findData(loadedEquipmentId_));
            }
            noteStatus(result.error.isEmpty() ? QStringLiteral("共享零件资料无效，原草稿保留。") : result.error, true);
            setBusy(false);
            return;
        }
        QString wanted = loadedEquipmentId_ == id ? loadedPartId_ : QString();
        if (dirty_ && loadedEquipmentId_ == id && !loadedPartId_.isEmpty()) {
            bool present = false;
            for (const auto &part : parts) present = present || part.id == loadedPartId_;
            if (!present) {
                noteStatus(QStringLiteral("共享资料缺少当前关联零件，草稿和关联范围已保留。"), true);
                setBusy(false); return;
            }
        }
        if (loadedEquipmentId_ != id) {
            displayNote({}, false);
            if (controller_.equipment() && controller_.equipment()->id == id) wanted = controller_.selectedPartId();
        }
        loadedEquipmentId_ = id;
        {
            const QSignalBlocker blocker(partScope_);
            partScope_->clear();
            partScope_->addItem(QStringLiteral("整台设备（所有批注）"), QString());
            for (const auto &part : parts) partScope_->addItem(QStringLiteral("%1 (%2)").arg(part.name, part.id), part.id);
            const int index = partScope_->findData(wanted);
            partScope_->setCurrentIndex(index >= 0 ? index : 0);
        }
        loadedPartId_ = partScope_->currentData().toString();
        if (currentNote_.isEmpty()) displayNote({}, dirty_);
        setBusy(false);
        refreshNotes();
    });
}

void CollaborationDialog::refreshNotes()
{
    if (!hasScope() || !configure()) return;
    const QString id = loadedEquipmentId_;
    QString path = QStringLiteral("/api/annotations?equipmentId=") + encoded(id);
    if (!loadedPartId_.isEmpty()) path += QStringLiteral("&partId=") + encoded(loadedPartId_);
    setBusy(true);
    const QString partId = loadedPartId_;
    request("GET", path, {}, [this, id, partId](ApiResult result) {
        if (!result.ok() || !result.data.isArray()) {
            noteStatus(result.error.isEmpty() ? QStringLiteral("批注列表格式无效，草稿保留。") : result.error, true);
            setBusy(false); return;
        }
        const auto array = result.data.toArray();
        if (array.size() > 200) {
            noteStatus(QStringLiteral("批注列表超过约定范围，原草稿保留。"), true);
            setBusy(false); return;
        }
        for (const auto &value : array) {
            if (!value.isObject() || !validNote(value.toObject(), id)
                || (!partId.isEmpty() && value.toObject().value(QStringLiteral("partId")).toString() != partId)) {
                noteStatus(QStringLiteral("服务批注格式无效，原内容保留。"), true);
                setBusy(false); return;
            }
        }
        const QString wanted = currentNote_.value(QStringLiteral("id")).toString(createRequestId_);
        QJsonObject selected;
        {
            const QSignalBlocker blocker(notes_);
            notes_->clear();
            for (const auto &value : array) {
                const auto note = value.toObject();
                const QString scope = note.value(QStringLiteral("partId")).isNull() ? QStringLiteral("整台设备") : note.value(QStringLiteral("partId")).toString();
                const QString preview = note.value(QStringLiteral("text")).toString().replace(QLatin1Char('\n'), QLatin1Char(' ')).left(64);
                auto *item = new QListWidgetItem(QStringLiteral("[%1] %2 · v%3").arg(scope, preview, QString::number(note.value(QStringLiteral("revision")).toInt())), notes_);
                item->setData(Qt::UserRole, QJsonDocument(note).toJson(QJsonDocument::Compact));
                if (note.value(QStringLiteral("id")).toString() == wanted) { selected = note; notes_->setCurrentItem(item); }
            }
        }
        if (!selected.isEmpty()) displayNote(selected, dirty_);
        else if (!dirty_) displayNote({}, false);
        if (selected.isEmpty() && dirty_ && !wanted.isEmpty())
            noteStatus(QStringLiteral("所选批注不在最近200条内，未取得新修订；原草稿和版本保留。"), true);
        else noteStatus(dirty_ ? QStringLiteral("最新保存版本已刷新；草稿保留，请检查后显式提交。") : QStringLiteral("批注已刷新。"));
        setBusy(false);
    });
}

void CollaborationDialog::displayNote(const QJsonObject &note, bool preserveDraft)
{
    currentNote_ = note;
    saved_->setPlainText(note.value(QStringLiteral("text")).toString());
    if (!preserveDraft) {
        const QSignalBlocker blocker(draft_);
        draft_->setPlainText(note.value(QStringLiteral("text")).toString());
        createRequestId_.clear(); createPayload_.clear();
    }
    dirty_ = draft_->toPlainText() != note.value(QStringLiteral("text")).toString();
    if (note.isEmpty()) noteMeta_->setText(QStringLiteral("新批注；关联：%1").arg(partScope_->currentText()));
    else {
        const auto updated = QDateTime::fromString(note.value(QStringLiteral("updatedAt")).toString(), Qt::ISODateWithMs).toUTC();
        noteMeta_->setText(QStringLiteral("作者：%1；更新者：%2；版本 %3；UTC %4")
            .arg(note.value(QStringLiteral("author")).toString(), note.value(QStringLiteral("updatedBy")).toString(),
                 QString::number(note.value(QStringLiteral("revision")).toInt()), updated.toString(Qt::ISODateWithMs)));
    }
    setBusy(busy_);
}

void CollaborationDialog::selectNote()
{
    const auto selected = notes_->selectedItems();
    const auto note = selected.isEmpty() ? QJsonObject() : QJsonDocument::fromJson(selected.first()->data(Qt::UserRole).toByteArray()).object();
    if (dirty_ && note.value(QStringLiteral("id")) != currentNote_.value(QStringLiteral("id"))) {
        const QSignalBlocker blocker(notes_);
        notes_->clearSelection();
        notes_->setCurrentRow(-1);
        for (int row = 0; row < notes_->count(); ++row) {
            const auto value = QJsonDocument::fromJson(notes_->item(row)->data(Qt::UserRole).toByteArray()).object();
            if (value.value(QStringLiteral("id")) == currentNote_.value(QStringLiteral("id"))) notes_->setCurrentRow(row);
        }
        noteStatus(QStringLiteral("草稿未保存，请先提交或放弃。"), true);
        return;
    }
    displayNote(note, false);
}

void CollaborationDialog::newNote()
{
    if (dirty_) { noteStatus(QStringLiteral("先保存或放弃当前草稿，再新建。"), true); return; }
    const QSignalBlocker blocker(notes_);
    notes_->clearSelection(); notes_->setCurrentRow(-1);
    displayNote({}, false);
    noteStatus(QStringLiteral("新批注关联当前范围。"));
}

void CollaborationDialog::discardDraft()
{
    displayNote(currentNote_, false);
    noteStatus(QStringLiteral("草稿已放弃，显示当前已保存内容。"));
}

void CollaborationDialog::saveNote()
{
    if (!hasScope() || !configure()) return;
    const QString text = draft_->toPlainText();
    const QString author = author_->text();
    if (text.trimmed().isEmpty() || text.size() > 2000 || author.trimmed().isEmpty()) {
        noteStatus(QStringLiteral("显示名不能为空；批注需非空白且不超过2000字符，草稿保留。"), true); return;
    }
    const bool updating = !currentNote_.isEmpty();
    QJsonObject payload;
    QString path;
    if (updating) {
        path = QStringLiteral("/api/annotations/") + currentNote_.value(QStringLiteral("id")).toString();
        payload = {{"text", text}, {"editor", author}, {"expectedRevision", currentNote_.value(QStringLiteral("revision"))}};
    } else {
        payload = {{"equipmentId", loadedEquipmentId_}, {"partId", loadedPartId_.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(loadedPartId_)}, {"text", text}, {"author", author}};
        const QByteArray identity = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        if (createRequestId_.isEmpty() || createPayload_ != identity) {
            createRequestId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
            createPayload_ = identity;
        }
        payload.insert(QStringLiteral("requestId"), createRequestId_);
        path = QStringLiteral("/api/annotations");
    }
    const QString id = loadedEquipmentId_;
    const QString expectedId = updating ? currentNote_.value(QStringLiteral("id")).toString() : createRequestId_;
    const QJsonValue expectedPart = updating ? currentNote_.value(QStringLiteral("partId")) : payload.value(QStringLiteral("partId"));
    setBusy(true);
    noteStatus(QStringLiteral("正在保存批注…"));
    request(updating ? "PUT" : "POST", path, payload, [this, id, text, expectedId, expectedPart](ApiResult result) {
        if (!result.ok() || !result.data.isObject() || !validNote(result.data.toObject(), id)) {
            noteStatus(result.error.isEmpty() ? QStringLiteral("批注结果无效，草稿保留。") : result.error + QStringLiteral(" 草稿已保留；刷新最新内容后再显式提交。"), true);
            setBusy(false); return;
        }
        const auto note = result.data.toObject();
        if (note.value(QStringLiteral("id")).toString() != expectedId || note.value(QStringLiteral("partId")) != expectedPart) {
            noteStatus(QStringLiteral("服务返回了不同批注或关联范围，原草稿保留。"), true);
            setBusy(false); return;
        }
        const bool laterRevision = note.value(QStringLiteral("text")).toString() != text;
        displayNote(note, laterRevision);
        createRequestId_.clear(); createPayload_.clear();
        setBusy(false);
        refreshNotes();
    });
}

void CollaborationDialog::publish()
{
    if (!controller_.equipment() || !configure()) return;
    const auto document = deviceDocument(*controller_.equipment(), controller_.parts());
    setBusy(true);
    status(QStringLiteral("正在发布当前设备…"));
    request("POST", QStringLiteral("/api/equipment"), document, [this](ApiResult result) {
        setBusy(false);
        status(result.ok() ? QStringLiteral("设备已发布，其他客户端可刷新共享库查看。") : result.error, !result.ok());
    });
}

void CollaborationDialog::download()
{
    if (equipment_->currentIndex() < 0 || !configure()) return;
    const QString id = equipment_->currentData().toString();
    const QString target = QStringLiteral("/api/equipment?id=") + QString::fromLatin1(QUrl::toPercentEncoding(id));
    setBusy(true);
    status(QStringLiteral("正在下载设备…"));
    request("GET", target, {}, [this, id](ApiResult result) {
        if (result.ok() && result.data.isObject()) {
            if (result.data.toObject().value(QStringLiteral("equipmentId")).toString() != id) {
                status(QStringLiteral("共享服务返回了不同设备，未导入资料。"), true);
                setBusy(false);
                return;
            }
            const QByteArray json = QJsonDocument(result.data.toObject()).toJson(QJsonDocument::Compact);
            if (controller_.importJson(json)) {
                if (onLocalImported) onLocalImported();
                status(QStringLiteral("已下载并保存到本地，可以离线查看。"));
            } else
                status(QStringLiteral("下载资料未覆盖本地：%1").arg(controller_.lastError()), true);
        } else
            status(result.error.isEmpty() ? QStringLiteral("共享资料格式不正确。") : result.error, true);
        setBusy(false);
    });
}
