#include "apiclient.h"
#include "equipmentcontroller.h"
#include "equipmentwindow.h"
#include "sceneview.h"
#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QNetworkReply>
#include <QPointer>
#include <QPushButton>
#include <QSqlDatabase>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <functional>
#include <utility>

namespace {
#define REQUIRE(condition) do { if (!(condition)) { \
    QTextStream(stderr) << "RESOURCE_FAIL " << __LINE__ << ": " << #condition << Qt::endl; \
    return false; } } while (false)

void drain()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
bool waitFor(const std::function<bool()> &condition, int timeout = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeout) QTest::qWait(5);
    drain();
    return condition();
}

/** 本进程loopback对端，不接真实API或数据库；挂起制造实际超时/取消。 */
class Peer : public QObject {
public:
    int mode = 0;
    int received = 0;
    explicit Peer(QObject *parent) : QObject(parent), server_(new QTcpServer(this))
    {
        server_->listen(QHostAddress::LocalHost, 0);
        connect(server_, &QTcpServer::newConnection, this, [this] {
            while (server_->hasPendingConnections()) {
                auto *socket = server_->nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    if (socket->property("handled").toBool()) return;
                    const auto bytes = socket->property("header").toByteArray() + socket->readAll();
                    socket->setProperty("header", bytes);
                    if (!bytes.contains("\r\n\r\n")) return;
                    socket->setProperty("handled", true);
                    ++received;
                    if (mode >= 2) return;
                    const QByteArray body = mode == 0 ? QByteArray(R"({"data":[]})")
                        : QByteArray(R"({"error":{"message":"controlled error"}})");
                    socket->write("HTTP/1.1 " + QByteArray(mode == 0 ? "200 OK" : "401 Unauthorized")
                        + "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size())
                        + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    bool listening() const { return server_->isListening(); }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server_->serverPort())); }
    int sockets() const { return server_->findChildren<QTcpSocket *>().size(); }
private:
    QTcpServer *server_;
};

QList<QPointer<QObject>> ownedObjects(QObject *owner)
{
    QList<QPointer<QObject>> result{QPointer<QObject>(owner)};
    for (auto *child : owner->findChildren<QObject *>()) result.append(QPointer<QObject>(child));
    return result;
}
bool allExpired(const QList<QPointer<QObject>> &objects)
{
    for (const auto &object : objects) if (object) return false;
    return true;
}

bool windows(const QString &database, int count, int loads, QString &renderer)
{
    for (int cycle = 0; cycle < count; ++cycle) {
        QList<QPointer<QObject>> objects;
        {
            EquipmentRepository repository;
            REQUIRE(repository.openDatabase(database) && repository.initialize());
            EquipmentController controller(repository);
            EquipmentWindow window(controller);
            for (int load = 0; load < loads; ++load) {
                REQUIRE(window.loadEquipment(QStringLiteral("EQ-001")));
                window.show();
                QCoreApplication::processEvents();
            }
            QTest::qWait(30);
            auto *scene = dynamic_cast<SceneView *>(window.findChild<QWidget *>("sceneView"));
            auto *parts = window.findChild<QListWidget *>("partsList");
            auto *hide = window.findChild<QPushButton *>("hideSelectedButton");
            auto *show = window.findChild<QPushButton *>("showAllButton");
            auto *shared = window.findChild<QPushButton *>("sharedLibraryButton");
            REQUIRE(scene && parts && parts->count() > 0 && hide && show && shared);
            renderer = scene->rendererDescription();
            QTest::mouseClick(parts->viewport(), Qt::LeftButton, Qt::NoModifier,
                              parts->visualItemRect(parts->item(0)).center());
            REQUIRE(!controller.selectedPartId().isEmpty());
            QTest::mouseClick(hide, Qt::LeftButton);
            QTest::mouseClick(show, Qt::LeftButton);
            REQUIRE(controller.hiddenPartIds().isEmpty());
            QTest::mouseClick(shared, Qt::LeftButton);
            QTest::qWait(5);
            REQUIRE(!window.findChildren<QDialog *>().isEmpty());
            // 捕获主窗、Scene、共享对话框的已知QObject子树，按父子所有权销毁后全部须失效。
            objects = ownedObjects(&window);
        }
        drain();
        REQUIRE(allExpired(objects));
    }
    return true;
}

bool requests(Peer &peer, int count)
{
    QList<QPointer<QObject>> objects;
    {
        ApiClient client;
        REQUIRE(client.configure(peer.url(), QStringLiteral("resource-fixture-token")));
        for (int index = 0; index < count; ++index) {
            peer.mode = index % 4;
            client.setTimeoutMs(peer.mode == 2 ? 50 : 500);
            bool done = false;
            ApiResult result;
            const int before = peer.received;
            client.request("GET", QStringLiteral("/api/equipment"), {}, [&](ApiResult value) {
                result = std::move(value); done = true;
            });
            if (peer.mode == 3) {
                REQUIRE(waitFor([&] { return peer.received > before; }));
                client.cancelAll();
            }
            REQUIRE(waitFor([&] { return done; }));
            REQUIRE(peer.received > before);
            REQUIRE(result.ok() == (peer.mode == 0));
            if (peer.mode == 1) REQUIRE(result.status == 401 && result.error == QStringLiteral("controlled error"));
            if (peer.mode == 2) REQUIRE(result.error.contains(QStringLiteral("超时")));
            if (peer.mode == 3) REQUIRE(result.error.contains(QStringLiteral("取消")));
            REQUIRE(client.pendingCount() == 0 && client.findChildren<QNetworkReply *>().isEmpty());
            REQUIRE(waitFor([&] { return peer.sockets() == 0; }));
        }
        objects = ownedObjects(&client);
    }
    drain();
    REQUIRE(allExpired(objects));
    return true;
}

QJsonObject counters(const Peer &peer)
{
    return {{"widgets", int(QApplication::allWidgets().size())},
            {"windows", int(QGuiApplication::allWindows().size())},
            {"applicationDescendants", int(QCoreApplication::instance()->findChildren<QObject *>().size())},
            {"sqlConnections", int(QSqlDatabase::connectionNames().size())}, {"peerSockets", peer.sockets()}};
}
void report(const QString &phase, int batch, const QJsonObject &counts, const QString &renderer, int accepted = 0)
{
    auto row = counts;
    row.insert("phase", phase);
    row.insert("batch", batch);
    row.insert("pid", QCoreApplication::applicationPid());
    row.insert("utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    row.insert("renderer", renderer);
    row.insert("accepted", accepted);
    QTextStream(stdout) << QJsonDocument(row).toJson(QJsonDocument::Compact) << Qt::endl;
}
int option(const QStringList &args, const QString &name, int fallback, int maximum)
{
    const int position = args.indexOf(name);
    if (position < 0) return fallback;
    if (position + 1 >= args.size()) return -1;
    bool ok = false;
    const int value = args[position + 1].toInt(&ok);
    return ok && value >= 1 && value <= maximum ? value : -1;
}
bool run()
{
    const auto args = QCoreApplication::arguments();
    const int batches = option(args, "--batches", 5, 20);
    const int windowCount = option(args, "--windows", 20, 100);
    const int loadCount = option(args, "--loads", 5, 20);
    const int requestCount = option(args, "--requests", 40, 500);
    REQUIRE(batches > 0 && windowCount > 0 && loadCount > 0 && requestCount >= 4);
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    Peer peer(QCoreApplication::instance());
    REQUIRE(peer.listening());
    QFile config(temporary.filePath(QStringLiteral("client.json")));
    REQUIRE(config.open(QIODevice::WriteOnly));
    REQUIRE(config.write(QJsonDocument(QJsonObject{{"host", "127.0.0.1"}, {"port", peer.url().port()},
                                                  {"token", "resource-fixture-token"}}).toJson()) > 0);
    config.close();
    qputenv("EQUIPMENT_SERVICE_CONFIG", config.fileName().toUtf8());
    const QString database = temporary.filePath(QStringLiteral("resources.sqlite"));
    const int originalSql = QSqlDatabase::connectionNames().size();
    QString renderer;
    REQUIRE(windows(database, 3, loadCount, renderer) && requests(peer, 8));
    // Scene有1200ms回退探针；采样前排空这类有界timer，避免把短暂排队当作持久增长。
    QTest::qWait(1400);
    drain();
    const auto baseline = counters(peer);
    REQUIRE(baseline.value("sqlConnections").toInt() == originalSql && peer.sockets() == 0);
    report("warm", 0, baseline, renderer);
    for (int batch = 1; batch <= batches; ++batch) {
        REQUIRE(windows(database, windowCount, loadCount, renderer) && requests(peer, requestCount));
        QTest::qWait(1400);
        drain();
        const auto current = counters(peer);
        REQUIRE(current == baseline);
        report("batch", batch, current, renderer);
    }
    report("complete", batches, counters(peer), renderer, 1);
    return true;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    return run() ? 0 : 1;
}
