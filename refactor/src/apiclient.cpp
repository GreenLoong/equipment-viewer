#include "apiclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <memory>

namespace {
constexpr qsizetype maximumResponseBytes = 6 * 1024 * 1024;
struct RequestState {
    QByteArray bytes;
    bool timeout = false;
    bool oversized = false;
};
}

ApiClient::ApiClient(QObject *parent) : QObject(parent), manager_(new QNetworkAccessManager(this)) {}

ApiClient::~ApiClient()
{
    closing_ = true;
    // abort可能同步发finished；先断开回调，避免析构中调用窗口或已销毁成员。
    const auto pending = pending_;
    for (QNetworkReply *reply : pending) {
        QObject::disconnect(reply, nullptr, this, nullptr);
        reply->abort();
    }
}

bool ApiClient::configure(const QUrl &base, const QString &token)
{
    if (!base.isValid() || base.host().isEmpty() || !base.userInfo().isEmpty()
        || !base.query().isEmpty() || !base.fragment().isEmpty()
        || (base.scheme() != QStringLiteral("http") && base.scheme() != QStringLiteral("https"))
        || token.trimmed().isEmpty() || token.contains(QLatin1Char('\r')) || token.contains(QLatin1Char('\n')))
        return false;
    base_ = base;
    token_ = token.toUtf8();
    return true;
}

void ApiClient::setTimeoutMs(int timeout) { timeoutMs_ = std::clamp(timeout, 50, 30000); }

void ApiClient::cancelAll()
{
    // finished与queued交付之间也可取消；不能只检查尚未完成的reply。
    ++generation_;
    const QPointer<ApiClient> owner(this);
    QList<QPointer<QNetworkReply>> pending;
    for (QNetworkReply *reply : pending_)
        pending.append(QPointer<QNetworkReply>(reply));
    for (const auto &reply : pending) {
        if (!owner) return;
        if (reply) {
            reply->setProperty("evCancelled", true);
            reply->abort();
        }
    }
}

void ApiClient::dispatch(std::function<void(ApiResult)> callback, ApiResult result, quint64 generation)
{
    const QPointer<ApiClient> owner(this);
    // 不在reply的abort/finished栈上调用用户代码，允许回调销毁客户端。
    QTimer::singleShot(0, this, [owner, generation, callback = std::move(callback), result = std::move(result)]() mutable {
        if (!owner) return;
        if (owner->generation_ != generation) {
            result.data = {};
            result.error = QStringLiteral("已取消等待；服务端操作可能已提交，请刷新确认。");
        }
        callback(std::move(result));
    });
}

void ApiClient::request(const QByteArray &method, const QString &path, const QJsonValue &body,
                        std::function<void(ApiResult)> callback)
{
    if (closing_)
        return;
    const quint64 generation = generation_;
    if (base_.isEmpty() || pending_.size() >= 8 || !path.startsWith(QStringLiteral("/api/"))) {
        dispatch(std::move(callback), {0, {}, QStringLiteral("共享连接配置无效或请求过多，请稍后重试。")}, generation);
        return;
    }
    // 查询参数传递设备ID，避免点ID或斜线在URL路径归一化后失去原身份。
    const QByteArray encoded = base_.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment)
        .toEncoded() + path.toUtf8();
    QNetworkRequest request(QUrl::fromEncoded(encoded, QUrl::StrictMode));
    request.setRawHeader("Authorization", "Bearer " + token_);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=utf-8"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QByteArray payload;
    if (body.isObject())
        payload = QJsonDocument(body.toObject()).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = method == "GET" ? manager_->get(request)
        : manager_->sendCustomRequest(request, method, payload);
    reply->setReadBufferSize(maximumResponseBytes);
    pending_.insert(reply);
    auto state = std::make_shared<RequestState>();
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [reply, state] {
        state->timeout = true;
        reply->abort();
    });
    connect(reply, &QNetworkReply::readyRead, this, [reply, state] {
        state->bytes += reply->readAll();
        if (state->bytes.size() > maximumResponseBytes) {
            state->oversized = true;
            state->bytes.clear();
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, timer, state, generation, callback = std::move(callback)]() mutable {
        timer->stop();
        pending_.remove(reply);
        ApiResult result;
        result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->property("evCancelled").toBool())
            result.error = QStringLiteral("已取消等待；服务端操作可能已提交，请刷新确认。" );
        else if (state->timeout)
            result.error = QStringLiteral("共享请求超时，原本地资料已保留；可刷新确认服务状态。" );
        else if (state->oversized)
            result.error = QStringLiteral("服务响应超过大小限制，未导入任何资料。" );
        else if (result.status >= 200 && result.status < 300 && reply->error() != QNetworkReply::NoError)
            result.error = QStringLiteral("共享响应传输未完整完成，未导入资料。" );
        else {
            state->bytes += reply->readAll();
            if (state->bytes.size() > maximumResponseBytes)
                result.error = QStringLiteral("服务响应超过大小限制。" );
            else {
                QJsonParseError parseError;
                const auto json = QJsonDocument::fromJson(state->bytes, &parseError);
                if (parseError.error == QJsonParseError::NoError && json.isObject()) {
                    const auto object = json.object();
                    result.data = object.value(QStringLiteral("data"));
                    if (result.status < 200 || result.status >= 300)
                        result.error = object.value(QStringLiteral("error")).toObject()
                            .value(QStringLiteral("message")).toString(QStringLiteral("共享服务拒绝请求。"));
                    else if (result.data.isUndefined())
                        result.error = QStringLiteral("共享服务返回的数据格式不完整。" );
                } else
                    result.error = QStringLiteral("共享服务无响应或返回了无效数据，请检查连接。" );
            }
        }
        reply->deleteLater();
        if (!closing_)
            dispatch(std::move(callback), std::move(result), generation);
    });
    timer->start(timeoutMs_);
}
