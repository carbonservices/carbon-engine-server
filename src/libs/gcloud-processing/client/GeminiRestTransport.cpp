#include "GeminiRestTransport.h"
#include "../rate_limiter/RequestPacer.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

static QString sanitizeLogString(const QString& str)
{
    static const QRegularExpression keyRegex(R"((key=)[^&\s]+)", QRegularExpression::CaseInsensitiveOption);
    QString sanitized = str;
    sanitized.replace(keyRegex, "\\1[REDACTED]");
    return sanitized;
}

QUrl GeminiRestTransport::buildUrl(const QString& model, const QString& method, const QString& apiKey)
{
    QString cleanModel = model.startsWith("models/") ? model.mid(7) : model;
    QString urlStr = QString("%1/%2:%3").arg(kApiBaseUrl, cleanModel, method);
    QUrl url(urlStr);
    if (!apiKey.isEmpty()) {
        QUrlQuery q;
        q.addQueryItem("key", apiKey);
        url.setQuery(q);
    }
    return url;
}

GeminiHttpResponse GeminiRestTransport::postJson(const QUrl& url, const QJsonObject& body, int timeoutMs, const QString& explicitApiKey)
{
    GeminiHttpResponse response;
    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QUrl cleanUrl = url;
    QString apiKey = explicitApiKey;
    if (cleanUrl.hasQuery()) {
        QUrlQuery q(cleanUrl);
        if (q.hasQueryItem("key")) {
            if (apiKey.isEmpty()) {
                apiKey = q.queryItemValue("key");
            }
            q.removeQueryItem("key");
            cleanUrl.setQuery(q);
        }
    }

    QNetworkAccessManager manager;
    QNetworkRequest request{cleanUrl};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!apiKey.isEmpty()) {
        request.setRawHeader("x-goog-api-key", apiKey.toUtf8());
    }

    QElapsedTimer timer;
    timer.start();

    QNetworkReply* reply = manager.post(request, payload);
    QEventLoop loop;
    QTimer abortTimer;
    abortTimer.setSingleShot(true);

    QObject::connect(&abortTimer, &QTimer::timeout, &loop, [&]() {
        if (reply->isRunning()) {
            reply->abort();
        }
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    abortTimer.start(timeoutMs);
    loop.exec();

    response.latencyMs = timer.elapsed();
    response.httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        if (response.httpStatusCode == 429) {
            RequestPacer::instance().reportRateLimitError();
        }

        QByteArray errData = reply->readAll();
        QJsonDocument errDoc = QJsonDocument::fromJson(errData);
        QString apiErrMsg;
        if (errDoc.isObject() && errDoc.object().contains("error")) {
            apiErrMsg = errDoc.object().value("error").toObject().value("message").toString();
        }

        response.success = false;
        response.errorMessage = sanitizeLogString(apiErrMsg.isEmpty()
            ? QString("Gemini API error (%1): %2").arg(response.httpStatusCode).arg(reply->errorString())
            : QString("Gemini API error (%1): %2").arg(response.httpStatusCode).arg(apiErrMsg));

        reply->deleteLater();
        return response;
    }

    RequestPacer::instance().reportSuccess();

    response.rawData = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(response.rawData);
    if (doc.isObject()) {
        response.json = doc.object();
        response.success = true;
    } else {
        response.success = false;
        response.errorMessage = "Invalid JSON response envelope from Gemini API";
    }

    return response;
}

GeminiHttpResponse GeminiRestTransport::getJson(const QUrl& url, int timeoutMs, const QString& explicitApiKey)
{
    GeminiHttpResponse response;
    QUrl cleanUrl = url;
    QString apiKey = explicitApiKey;
    if (cleanUrl.hasQuery()) {
        QUrlQuery q(cleanUrl);
        if (q.hasQueryItem("key")) {
            if (apiKey.isEmpty()) {
                apiKey = q.queryItemValue("key");
            }
            q.removeQueryItem("key");
            cleanUrl.setQuery(q);
        }
    }

    QNetworkAccessManager manager;
    QNetworkRequest request{cleanUrl};
    if (!apiKey.isEmpty()) {
        request.setRawHeader("x-goog-api-key", apiKey.toUtf8());
    }

    QElapsedTimer timer;
    timer.start();

    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QTimer abortTimer;
    abortTimer.setSingleShot(true);

    QObject::connect(&abortTimer, &QTimer::timeout, &loop, [&]() {
        if (reply->isRunning()) {
            reply->abort();
        }
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    abortTimer.start(timeoutMs);
    loop.exec();

    response.latencyMs = timer.elapsed();
    response.httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        if (response.httpStatusCode == 429) {
            RequestPacer::instance().reportRateLimitError();
        }

        QByteArray errData = reply->readAll();
        QJsonDocument errDoc = QJsonDocument::fromJson(errData);
        QString apiErrMsg;
        if (errDoc.isObject() && errDoc.object().contains("error")) {
            apiErrMsg = errDoc.object().value("error").toObject().value("message").toString();
        }

        response.success = false;
        response.errorMessage = sanitizeLogString(apiErrMsg.isEmpty()
            ? QString("Gemini API error (%1): %2").arg(response.httpStatusCode).arg(reply->errorString())
            : QString("Gemini API error (%1): %2").arg(response.httpStatusCode).arg(apiErrMsg));

        reply->deleteLater();
        return response;
    }

    RequestPacer::instance().reportSuccess();

    response.rawData = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(response.rawData);
    if (doc.isObject()) {
        response.json = doc.object();
        response.success = true;
    } else {
        response.success = false;
        response.errorMessage = "Invalid JSON response envelope from Gemini API";
    }

    return response;
}

