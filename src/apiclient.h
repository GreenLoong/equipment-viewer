#pragma once

#include <QObject>
#include <QJsonValue>
#include <QSet>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

/** HTTP结果，业务错误不抛出异常、不包含访问令牌。 */
struct ApiResult {
    int status = 0;
    QJsonValue data;
    QString error;
    bool ok() const { return error.isEmpty() && status >= 200 && status < 300; }
};

/** 有界异步HTTP客户端；超时、取消和析构时释放回复及缓冲。 */
class ApiClient : public QObject {
public:
    /** 管理器由本对象拥有；对象应在其Qt线程创建和使用。 */
    explicit ApiClient(QObject *parent = nullptr);
    /** 析构时断开回调并取消尚未完成的请求。 */
    ~ApiClient() override;
    /** 设置基地址和令牌；只允许无用户名/密码的HTTP(S)地址。 */
    bool configure(const QUrl &base, const QString &token);
    /** 异步发送固定业务路径；回调在所属线程执行，析构后不再调用。 */
    void request(const QByteArray &method, const QString &path, const QJsonValue &body,
                 std::function<void(ApiResult)> callback);
    /** 测试/配置可调整等待期限，最少50毫秒、最多30秒。 */
    void setTimeoutMs(int timeout);
    /** 取消当前等待并通知回调；不保证服务端尚未提交，需刷新确认。 */
    void cancelAll();
    /** 当前未完成请求数，用于资源与取消验证。 */
    int pendingCount() const { return pending_.size(); }

private:
    void dispatch(std::function<void(ApiResult)> callback, ApiResult result, quint64 generation);
    QNetworkAccessManager *manager_;
    QUrl base_;
    QByteArray token_;
    QSet<QNetworkReply *> pending_;
    int timeoutMs_ = 8000;
    bool closing_ = false;
    quint64 generation_ = 0;
};
