#include "apiclient.h"
#include "collaborationdialog.h"
#include "equipmentcontroller.h"
#include "equipmentwindow.h"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <functional>

namespace {
const QString equipmentId = QStringLiteral("REVIEW-EQ");
const QString partP = QStringLiteral("P/one ");
const QString partQ = QStringLiteral("Q");
const QString noteId = QStringLiteral("11111111-1111-4111-8111-111111111111");
const QString differentId = QStringLiteral("22222222-2222-4222-8222-222222222222");
const QString savedText = QStringLiteral("saved-v1");

#define REQUIRE(condition) do { if (!(condition)) { \
    QTextStream(stderr) << "REVIEWER FAULT FAIL " << __LINE__ << ": " << #condition << Qt::endl; \
    return false; } } while (false)

bool waitFor(const std::function<bool()> &condition, int timeout = 5000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < timeout)
        QTest::qWait(5);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return condition();
}

QJsonObject part(const QString &id)
{
    return {{"id", id}, {"name", "same-name"}, {"material", "fixture"},
            {"shape", "box"}, {"dimensions", QJsonArray{10, 8, 6}},
            {"position", QJsonArray{id == partP ? 0 : 20, 0, 0}}};
}

QJsonObject device(bool omitP = false)
{
    QJsonArray parts;
    if (!omitP) parts.append(part(partP));
    parts.append(part(partQ));
    return {{"equipmentId", equipmentId}, {"equipmentName", "review fixture"}, {"parts", parts}};
}

QJsonObject note(const QString &id = noteId, const QString &scope = partP,
                 const QString &text = savedText, const QString &author = QStringLiteral("fixture-author"))
{
    return {{"id", id}, {"equipmentId", equipmentId}, {"partId", scope},
            {"text", text}, {"author", author}, {"updatedBy", author}, {"revision", 1},
            {"createdAt", "2026-10-08T00:00:00.000Z"}, {"updatedAt", "2026-10-08T00:00:00.000Z"}};
}

/** 真实loopback TCP，仅故障注入HTTP DTO；不接真实API/MySQL，不调用业务回调。 */
class FaultPeer : public QObject {
public:
    enum class SaveFault { None, WrongId, WrongPart };
    bool omitPart = false;
    bool wrongNotePart = false;
    bool omitCurrentNote = false;
    bool holdNotes = false;
    SaveFault saveFault = SaveFault::None;
    int noteGets = 0;
    int heldCount = 0;
    QJsonObject lastPost;
    QJsonObject lastPut;
    QString lastPutId;
    QString lastQueryPart;

    FaultPeer() : server_(new QTcpServer(this))
    {
        server_->listen(QHostAddress::LocalHost, 0);
        connect(server_, &QTcpServer::newConnection, this, [this] {
            while (server_->hasPendingConnections()) {
                auto *socket = server_->nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    if (socket->property("handled").toBool()) return;
                    QByteArray bytes = socket->property("buffer").toByteArray() + socket->readAll();
                    socket->setProperty("buffer", bytes);
                    const int headersEnd = bytes.indexOf("\r\n\r\n");
                    if (headersEnd < 0) return;
                    int contentLength = 0;
                    const auto headers = bytes.left(headersEnd).split('\n');
                    for (const auto &line : headers) {
                        if (line.toLower().startsWith("content-length:"))
                            contentLength = line.mid(line.indexOf(':') + 1).trimmed().toInt();
                    }
                    if (contentLength < 0 || contentLength > 16384) {
                        socket->abort(); return;
                    }
                    if (bytes.size() < headersEnd + 4 + contentLength) return;
                    socket->setProperty("handled", true);
                    const auto requestLine = headers.first().trimmed().split(' ');
                    if (requestLine.size() != 3) { socket->abort(); return; }
                    const auto url = QUrl::fromEncoded("http://fixture" + requestLine[1]);
                    route(socket, requestLine[0], url,
                          QJsonDocument::fromJson(bytes.mid(headersEnd + 4, contentLength)).object());
                });
            }
        });
    }

    quint16 port() const { return server_->serverPort(); }
    bool listening() const { return server_->isListening(); }

    void releaseLatestNotes()
    {
        for (int index = held_.size() - 1; index >= 0; --index) {
            if (held_[index] && held_[index]->state() == QAbstractSocket::ConnectedState) {
                send(held_[index], notes());
                return;
            }
        }
    }

private:
    QJsonArray notes() const
    {
        if (wrongNotePart) return {note(noteId, partQ)};
        if (!omitCurrentNote) return {note()};
        QJsonArray recent;
        for (int index = 0; index < 200; ++index) {
            const QString id = QStringLiteral("33333333-3333-4333-8333-%1")
                .arg(index, 12, 16, QLatin1Char('0'));
            recent.append(note(id, partP, QStringLiteral("recent-%1").arg(index)));
        }
        return recent;
    }

    void send(QTcpSocket *socket, const QJsonValue &data)
    {
        const QByteArray body = QJsonDocument(QJsonObject{{"data", data}}).toJson(QJsonDocument::Compact);
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                      + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    void route(QTcpSocket *socket, const QByteArray &method, const QUrl &url, const QJsonObject &body)
    {
        const QUrlQuery query(url);
        if (url.path() == QStringLiteral("/api/equipment")) {
            if (query.hasQueryItem(QStringLiteral("id"))) send(socket, device(omitPart));
            else send(socket, QJsonArray{QJsonObject{{"id", equipmentId}, {"name", "review fixture"},
                                                    {"partCount", 2}, {"revision", 1}}});
        } else if (url.path() == QStringLiteral("/api/annotations") && method == "GET") {
            ++noteGets;
            lastQueryPart = query.queryItemValue(QStringLiteral("partId"), QUrl::FullyDecoded);
            if (holdNotes) { ++heldCount; held_.append(QPointer<QTcpSocket>(socket)); }
            else send(socket, notes());
        } else if (url.path() == QStringLiteral("/api/annotations") && method == "POST") {
            lastPost = body;
            send(socket, note(saveFault == SaveFault::WrongId ? differentId : body.value("requestId").toString(),
                              saveFault == SaveFault::WrongPart ? partQ : body.value("partId").toString(),
                              body.value("text").toString(), body.value("author").toString()));
        } else if (url.path().startsWith(QStringLiteral("/api/annotations/")) && method == "PUT") {
            lastPut = body;
            lastPutId = url.path().mid(17);
            auto result = note(saveFault == SaveFault::WrongId ? differentId : lastPutId,
                               saveFault == SaveFault::WrongPart ? partQ : partP,
                               body.value("text").toString());
            result.insert("revision", 2);
            result.insert("updatedBy", body.value("editor"));
            send(socket, result);
        } else {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
        }
    }

    QTcpServer *server_;
    QList<QPointer<QTcpSocket>> held_;
};

struct Controls {
    QPointer<CollaborationDialog> dialog;
    ApiClient *client = nullptr;
    QComboBox *scope = nullptr;
    QListWidget *list = nullptr;
    QPlainTextEdit *draft = nullptr;
    QPlainTextEdit *saved = nullptr;
    QLabel *meta = nullptr;
    QLabel *status = nullptr;
    QPushButton *catalog = nullptr;
    QPushButton *refresh = nullptr;
    QPushButton *save = nullptr;
    QPushButton *discard = nullptr;
    QPushButton *close = nullptr;

    bool open(EquipmentWindow &window)
    {
        window.activateWindow();
        auto *button = window.findChild<QPushButton *>(QStringLiteral("sharedLibraryButton"));
        REQUIRE(button);
        QTest::mouseClick(button, Qt::LeftButton);
        REQUIRE(waitFor([&] {
            for (auto *candidate : window.findChildren<QDialog *>()) {
                if (candidate->isVisible()) {
                    if (auto *shared = dynamic_cast<CollaborationDialog *>(candidate)) {
                        dialog = shared; return true;
                    }
                }
            }
            return false;
        }));
        scope = dialog->findChild<QComboBox *>("annotationPart");
        list = dialog->findChild<QListWidget *>("annotationList");
        draft = dialog->findChild<QPlainTextEdit *>("annotationDraft");
        saved = dialog->findChild<QPlainTextEdit *>("annotationSaved");
        meta = dialog->findChild<QLabel *>("annotationMeta");
        status = dialog->findChild<QLabel *>("annotationStatus");
        catalog = dialog->findChild<QPushButton *>("sharedRefreshButton");
        refresh = dialog->findChild<QPushButton *>("annotationRefreshButton");
        save = dialog->findChild<QPushButton *>("annotationSaveButton");
        discard = dialog->findChild<QPushButton *>("annotationDiscardButton");
        close = nullptr;
        client = nullptr;
        for (auto *candidate : dialog->findChildren<QPushButton *>())
            if (candidate->text() == QStringLiteral("关闭")) close = candidate;
        for (auto *candidate : dialog->findChildren<QObject *>())
            if (auto *api = dynamic_cast<ApiClient *>(candidate)) client = api;
        REQUIRE(scope && list && draft && saved && meta && status && catalog && refresh && save && discard && close && client);
        return true;
    }

    bool click(QPushButton *button)
    {
        REQUIRE(button->isEnabled());
        QTest::mouseClick(button, Qt::LeftButton);
        REQUIRE(waitFor([&] { return dialog && catalog->isEnabled() && client->pendingCount() == 0; }));
        return true;
    }
};

void typeDraft(QPlainTextEdit *draft, const char *text)
{
    draft->setFocus();
    QTest::keyClick(draft, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(draft, text);
}

bool run()
{
    FaultPeer peer;
    REQUIRE(peer.listening());
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString configPath = temporary.filePath(QStringLiteral("review-client.json"));
    QFile config(configPath);
    REQUIRE(config.open(QIODevice::WriteOnly));
    REQUIRE(config.write(QJsonDocument(QJsonObject{{"host", "127.0.0.1"}, {"port", int(peer.port())},
                                                  {"token", "review-fault-token"}}).toJson()) > 0);
    config.close();
    qputenv("EQUIPMENT_SERVICE_CONFIG", configPath.toUtf8());
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(temporary.filePath(QStringLiteral("review.sqlite"))) && repository.initialize());
    EquipmentController controller(repository);
    EquipmentWindow window(controller);
    REQUIRE(window.importData(QJsonDocument(device()).toJson()));
    window.show();
    QTest::qWait(150);
    auto *parts = window.findChild<QListWidget *>("partsList");
    REQUIRE(parts);
    for (int row = 0; row < parts->count(); ++row)
        if (parts->item(row)->data(Qt::UserRole).toString() == partP)
            QTest::mouseClick(parts->viewport(), Qt::LeftButton, Qt::NoModifier,
                              parts->visualItemRect(parts->item(row)).center());
    REQUIRE(controller.selectedPartId() == partP);

    Controls c;
    REQUIRE(c.open(window) && c.click(c.catalog));
    REQUIRE(peer.noteGets > 0 && peer.lastQueryPart == partP && c.scope->currentData().toString() == partP);
    typeDraft(c.draft, "unsaved-draft");
    REQUIRE(c.save->isEnabled());
    QTest::keyClick(c.draft, Qt::Key_Escape);
    REQUIRE(c.dialog && c.dialog->isVisible() && c.draft->toPlainText() == QStringLiteral("unsaved-draft"));
    REQUIRE(c.status->text().contains(QStringLiteral("草稿未提交")));
    REQUIRE(!c.dialog->close());
    REQUIRE(c.dialog && c.dialog->isVisible() && c.draft->toPlainText() == QStringLiteral("unsaved-draft"));
    QTest::mouseClick(c.close, Qt::LeftButton);
    REQUIRE(c.dialog && c.dialog->isVisible() && c.draft->toPlainText() == QStringLiteral("unsaved-draft"));
    REQUIRE(controller.selectedPartId() == partP);
    QTextStream(stdout) << "REVIEW_FAULT dirtyEscape=1 closeEvent=1 closeButton=1" << Qt::endl;

    peer.omitPart = true;
    REQUIRE(c.click(c.catalog));
    REQUIRE(c.status->text().contains(QStringLiteral("缺少当前关联零件")));
    REQUIRE(c.scope->currentData().toString() == partP && c.draft->toPlainText() == QStringLiteral("unsaved-draft"));
    peer.omitPart = false;
    peer.saveFault = FaultPeer::SaveFault::WrongId;
    REQUIRE(c.click(c.save));
    REQUIRE(c.status->text().contains(QStringLiteral("不同批注")));
    REQUIRE(c.draft->toPlainText() == QStringLiteral("unsaved-draft") && c.saved->toPlainText().isEmpty() && c.save->isEnabled());
    REQUIRE(peer.lastPost.value("partId").toString() == partP);
    const QString nonce = peer.lastPost.value("requestId").toString();
    REQUIRE(!nonce.isEmpty());
    peer.saveFault = FaultPeer::SaveFault::WrongPart;
    REQUIRE(c.click(c.save));
    REQUIRE(c.status->text().contains(QStringLiteral("不同批注")));
    REQUIRE(c.draft->toPlainText() == QStringLiteral("unsaved-draft") && c.saved->toPlainText().isEmpty() && c.save->isEnabled());
    REQUIRE(peer.lastPost.value("requestId").toString() == nonce && peer.lastPost.value("partId").toString() == partP);
    QTextStream(stdout) << "REVIEW_FAULT missingPartPreservesScope=1 wrongSaveId=1 wrongSavePart=1 stableNonce=1" << Qt::endl;

    peer.wrongNotePart = true;
    REQUIRE(c.click(c.refresh));
    REQUIRE(peer.lastQueryPart == partP && c.status->text().contains(QStringLiteral("格式无效")));
    REQUIRE(c.draft->toPlainText() == QStringLiteral("unsaved-draft") && c.saved->toPlainText().isEmpty());
    REQUIRE(c.list->count() == 1);
    const auto previous = QJsonDocument::fromJson(c.list->item(0)->data(Qt::UserRole).toByteArray()).object();
    REQUIRE(previous.value("partId").toString() == partP);
    peer.wrongNotePart = false;
    REQUIRE(c.click(c.discard));
    REQUIRE(c.draft->toPlainText().isEmpty() && !c.save->isEnabled());
    QTest::mouseClick(c.close, Qt::LeftButton);
    REQUIRE(waitFor([&] { return c.dialog.isNull(); }));
    QTextStream(stdout) << "REVIEW_FAULT wrongFilteredScope=1 explicitDiscardCloses=1" << Qt::endl;

    REQUIRE(c.open(window) && c.click(c.catalog));
    REQUIRE(c.list->count() == 1);
    QTest::mouseClick(c.list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      c.list->visualItemRect(c.list->item(0)).center());
    REQUIRE(c.saved->toPlainText() == savedText && c.meta->text().contains(QStringLiteral("版本 1")));
    const QString originalMeta = c.meta->text();
    typeDraft(c.draft, "edited-draft");
    peer.saveFault = FaultPeer::SaveFault::WrongId;
    REQUIRE(c.click(c.save));
    REQUIRE(c.status->text().contains(QStringLiteral("不同批注")) && c.meta->text() == originalMeta);
    REQUIRE(c.saved->toPlainText() == savedText && c.draft->toPlainText() == QStringLiteral("edited-draft"));
    peer.saveFault = FaultPeer::SaveFault::WrongPart;
    REQUIRE(c.click(c.save));
    REQUIRE(c.status->text().contains(QStringLiteral("不同批注")) && c.meta->text() == originalMeta);
    REQUIRE(c.saved->toPlainText() == savedText && c.draft->toPlainText() == QStringLiteral("edited-draft"));
    REQUIRE(peer.lastPutId == noteId && peer.lastPut.value("expectedRevision").toInt() == 1);
    peer.saveFault = FaultPeer::SaveFault::None;
    QTextStream(stdout) << "REVIEW_FAULT wrongPutId=1 wrongPutPart=1 originalRevision=1" << Qt::endl;
    peer.omitCurrentNote = true;
    REQUIRE(c.click(c.refresh));
    REQUIRE(c.list->count() == 200 && c.status->text().contains(QStringLiteral("未取得新修订")));
    REQUIRE(c.saved->toPlainText() == savedText && c.meta->text() == originalMeta);
    REQUIRE(c.draft->toPlainText() == QStringLiteral("edited-draft") && c.save->text().contains(QStringLiteral("版本 1")));
    peer.omitCurrentNote = false;
    QTextStream(stdout) << "REVIEW_FAULT recent200PreservesMetadata=1" << Qt::endl;

    // 真实旧reply已结束并排队callback，但尚未交付；立即启动新请求。
    // 这里使用公开Dialog reject和控件click同步排列事件，不调用业务回调造假。
    peer.holdNotes = true;
    QTest::mouseClick(c.refresh, Qt::LeftButton);
    REQUIRE(waitFor([&] { return peer.heldCount == 1 && c.client->pendingCount() == 1; }));
    auto *oldReply = c.client->findChild<QNetworkReply *>();
    REQUIRE(oldReply);
    bool oldFinished = false;
    bool queuedTurnPassed = false;
    QObject::connect(oldReply, &QNetworkReply::finished, c.dialog, [&] {
        oldFinished = true;
        QTimer::singleShot(0, &window, [&] { queuedTurnPassed = true; });
    }, Qt::DirectConnection);
    c.dialog->reject();
    REQUIRE(oldFinished && c.dialog && c.dialog->isVisible() && c.refresh->isEnabled());
    c.refresh->click();
    REQUIRE(waitFor([&] { return peer.heldCount == 2 && queuedTurnPassed; }));
    REQUIRE(c.client->pendingCount() == 1 && !c.refresh->isEnabled() && !c.draft->isEnabled());
    REQUIRE(c.draft->toPlainText() == QStringLiteral("edited-draft"));
    peer.holdNotes = false;
    peer.releaseLatestNotes();
    REQUIRE(waitFor([&] { return c.refresh->isEnabled() && c.client->pendingCount() == 0; }));
    REQUIRE(c.draft->toPlainText() == QStringLiteral("edited-draft") && c.save->isEnabled());
    REQUIRE(c.click(c.discard));
    QTest::mouseClick(c.close, Qt::LeftButton);
    REQUIRE(waitFor([&] { return c.dialog.isNull(); }));
    QTextStream(stdout) << "REVIEW_FAULT cancelledOldCallbackKeepsNewBusy=1" << Qt::endl;
    return true;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const bool passed = run();
    QTextStream(stdout) << "REVIEW_ANNOTATION_FAULTS accepted=" << passed
                        << " controlledHttp=1 realMysql=0" << Qt::endl;
    return passed ? 0 : 1;
}
