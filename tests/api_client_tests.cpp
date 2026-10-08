#include "apiclient.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QJsonArray>
#include <QNetworkReply>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTextStream>
#include <memory>

#define REQUIRE(condition) do { if (!(condition)) { \
    QTextStream(stderr) << "FAIL " << __LINE__ << ": " << #condition << Qt::endl; return 1; } } while(false)

namespace {
/** 受控loopback对端，用真实HTTP验证回复、超时和取消；不访问正常服务。 */
class Peer : public QObject {
public:
    Peer(QByteArray body, int code = 200, bool silent = false, int extraLength = 0)
        : server_(new QTcpServer(this)), body_(std::move(body)), code_(code), silent_(silent), extraLength_(extraLength)
    {
        server_->listen(QHostAddress::LocalHost, 0);
        connect(server_, &QTcpServer::newConnection, this, [this] {
            auto *socket = server_->nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                const QByteArray bytes = socket->readAll();
                if (silent_ || socket->property("sent").toBool() || !bytes.contains("\r\n\r\n"))
                    return;
                socket->setProperty("sent", true);
                socket->write("HTTP/1.1 " + QByteArray::number(code_) + " Result\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(body_.size() + extraLength_) + "\r\nConnection: close\r\n\r\n" + body_);
                socket->disconnectFromHost();
            });
        });
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server_->serverPort())); }
private:
    QTcpServer *server_;
    QByteArray body_;
    int code_;
    bool silent_;
    int extraLength_;
};

bool waitFor(const std::function<bool()> &condition, int milliseconds = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < milliseconds)
        QTest::qWait(5);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return condition();
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Peer good(R"({"data":[{"id":"A"}]})");
    ApiClient client;
    REQUIRE(client.configure(good.url(), QStringLiteral("test-token")));
    REQUIRE(!client.configure(good.url(), QStringLiteral("bad\r\nheader")));
    REQUIRE(!client.configure(QUrl(QStringLiteral("http://user:password@localhost")), QStringLiteral("test-token")));
    bool done = false;
    ApiResult result;
    client.request("GET", QStringLiteral("/api/equipment"), {}, [&](ApiResult value) { result = value; done = true; });
    REQUIRE(waitFor([&] { return done; }));
    REQUIRE(result.ok() && result.data.isArray() && client.pendingCount() == 0);
    REQUIRE(client.findChildren<QNetworkReply *>().isEmpty());

    // 完整JSON不代表传输完整：声明更长Content-Length后断连必须拒绝。
    Peer truncated(R"({"data":[{"id":"A"}]})", 200, false, 1);
    REQUIRE(client.configure(truncated.url(), QStringLiteral("test-token")));
    done = false;
    client.request("GET", QStringLiteral("/api/equipment"), {}, [&](ApiResult value) { result = value; done = true; });
    REQUIRE(waitFor([&] { return done; }));
    REQUIRE(!result.ok() && result.error.contains(QStringLiteral("未完整")));

    // 在finished已排队成功交付、用户尚未收到时取消，不能再送成功结果。
    REQUIRE(client.configure(good.url(), QStringLiteral("test-token")));
    done = false;
    bool cancelledBetween = false;
    client.request("GET", QStringLiteral("/api/equipment"), {}, [&](ApiResult value) { result = value; done = true; });
    auto *queuedReply = client.findChild<QNetworkReply *>();
    REQUIRE(queuedReply);
    QObject::connect(queuedReply, &QNetworkReply::finished, &app, [&] {
        cancelledBetween = !done && client.pendingCount() == 0;
        client.cancelAll();
    }, Qt::DirectConnection);
    REQUIRE(waitFor([&] { return done; }));
    REQUIRE(cancelledBetween && !result.ok() && result.error.contains(QStringLiteral("取消")));

    Peer denied(R"({"error":{"message":"denied"}})", 401);
    REQUIRE(client.configure(denied.url(), QStringLiteral("test-token")));
    done = false;
    client.request("GET", QStringLiteral("/api/equipment"), {}, [&](ApiResult value) { result = value; done = true; });
    REQUIRE(waitFor([&] { return done; }));
    REQUIRE(!result.ok() && result.status == 401 && result.error == QStringLiteral("denied"));

    Peer invalid("not JSON");
    REQUIRE(client.configure(invalid.url(), QStringLiteral("test-token")));
    done = false;
    client.request("GET", QStringLiteral("/api/equipment"), {}, [&](ApiResult value) { result = value; done = true; });
    REQUIRE(waitFor([&] { return done; }) && !result.ok());

    Peer stalled({}, 200, true);
    REQUIRE(client.configure(stalled.url(), QStringLiteral("test-token")));
    client.setTimeoutMs(50);
    done = false;
    client.request("GET", QStringLiteral("/api/equipment"), {}, [&](ApiResult value) { result = value; done = true; });
    REQUIRE(waitFor([&] { return done; }));
    REQUIRE(!result.ok() && result.error.contains(QStringLiteral("超时")) && client.pendingCount() == 0);
    REQUIRE(client.findChildren<QNetworkReply *>().isEmpty());

    client.setTimeoutMs(8000);
    done = false;
    client.request("GET", QStringLiteral("/api/equipment"), {}, [&](ApiResult value) { result = value; done = true; });
    client.cancelAll();
    REQUIRE(waitFor([&] { return done; }));
    REQUIRE(result.error.contains(QStringLiteral("取消")) && client.pendingCount() == 0);
    bool destroyedCallback = false;
    auto *temporary = new ApiClient;
    REQUIRE(temporary->configure(stalled.url(), QStringLiteral("test-token")));
    temporary->request("GET", QStringLiteral("/api/equipment"), {}, [&](ApiResult) { destroyedCallback = true; });
    delete temporary;
    QTest::qWait(100);
    REQUIRE(!destroyedCallback);

    // 第一个取消回调销毁拥有者；另一个reply不得被裸指针循环或队列再访问。
    auto selfDestroy = std::make_unique<ApiClient>();
    REQUIRE(selfDestroy->configure(stalled.url(), QStringLiteral("test-token")));
    const QPointer<ApiClient> guarded(selfDestroy.get());
    int cancellingCallbacks = 0;
    for (int i = 0; i < 2; ++i)
        selfDestroy->request("GET", QStringLiteral("/api/equipment"), {}, [&](ApiResult value) {
            if (value.error.contains(QStringLiteral("取消"))) ++cancellingCallbacks;
            selfDestroy.reset();
        });
    selfDestroy->cancelAll();
    REQUIRE(waitFor([&] { return guarded.isNull(); }));
    REQUIRE(cancellingCallbacks == 1);

    Peer oversized(QByteArray(6 * 1024 * 1024 + 1, 'x'));
    REQUIRE(client.configure(oversized.url(), QStringLiteral("test-token")));
    done = false;
    client.request("GET", QStringLiteral("/api/equipment"), {}, [&](ApiResult value) { result = value; done = true; });
    REQUIRE(waitFor([&] { return done; }));
    REQUIRE(result.error.contains(QStringLiteral("限制")) && client.pendingCount() == 0);

    // 同进程反复请求并排空DeferredDelete，确保回复对象不积累。
    REQUIRE(client.configure(good.url(), QStringLiteral("test-token")));
    for (int i = 0; i < 50; ++i) {
        done = false;
        client.request("GET", QStringLiteral("/api/equipment"), {}, [&](ApiResult value) { result = value; done = true; });
        REQUIRE(waitFor([&] { return done; }) && result.ok());
        REQUIRE(client.pendingCount() == 0 && client.findChildren<QNetworkReply *>().isEmpty());
    }
    QTextStream(stdout) << "API client passed: bounds, auth/error, timeout, cancellation, destruction, response release." << Qt::endl;
    return 0;
}
