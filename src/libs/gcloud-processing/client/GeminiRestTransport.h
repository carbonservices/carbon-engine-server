#pragma once

#include <QString>
#include <QJsonObject>
#include <QByteArray>
#include <QUrl>

struct GeminiHttpResponse {
    bool success{false};
    int httpStatusCode{0};
    QByteArray rawData;
    QJsonObject json;
    QString errorMessage;
    qint64 latencyMs{0};
};

class GeminiRestTransport {
public:
    static inline constexpr const char* kApiBaseUrl = "https://generativelanguage.googleapis.com/v1beta/models";

    static QUrl buildUrl(const QString& model, const QString& method, const QString& apiKey = QString());
    static GeminiHttpResponse postJson(const QUrl& url, const QJsonObject& body, int timeoutMs = 60000, const QString& apiKey = QString());
    static GeminiHttpResponse getJson(const QUrl& url, int timeoutMs = 20000, const QString& apiKey = QString());
};

