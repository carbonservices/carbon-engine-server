#include "GeminiEmbeddingClient.h"
#include "../client/GeminiRestTransport.h"
#include "../config/GCloudConfig.h"
#include "../rate_limiter/RequestPacer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

GeminiEmbeddingClient::GeminiEmbeddingClient(QObject* parent)
    : QObject(parent)
{
}

std::vector<float> GeminiEmbeddingClient::generateEmbedding(const std::string& text, QString& outError, const QString& explicitApiKey)
{
    QString apiKey = !explicitApiKey.isEmpty() ? explicitApiKey.trimmed() : GCloudConfig::instance().apiKey();
    if (apiKey.isEmpty()) {
        outError = "Gemini API key is required (pass via X-Gemini-API-Key header or api_key parameter)";
        return {};
    }

    RequestPacer::instance().acquireEmbeddingSlot();

    QString model = GCloudConfig::instance().embeddingModelName();
    QUrl url = GeminiRestTransport::buildUrl(model, "embedContent", apiKey);

    QJsonObject contentObj;
    QJsonArray partsArr;
    QJsonObject partText;
    partText["text"] = QString::fromStdString(text);
    partsArr.append(partText);
    contentObj["parts"] = partsArr;

    QJsonObject rootObj;
    rootObj["model"] = QString("models/%1").arg(model);
    rootObj["content"] = contentObj;
    if (model.contains("embedding-004")) {
        rootObj["outputDimensionality"] = 384;
    }

    auto httpResp = GeminiRestTransport::postJson(url, rootObj, 15000);
    if (!httpResp.success) {
        outError = httpResp.errorMessage;
        _log.error(outError);
        return {};
    }

    QJsonObject embObj = httpResp.json.value("embedding").toObject();
    QJsonArray vals = embObj.value("values").toArray();

    std::vector<float> result;
    result.reserve(vals.size());
    for (const auto& v : vals) {
        result.push_back(static_cast<float>(v.toDouble()));
    }

    _log.info(QString("[Cloud] Generated %1-dim embedding via %2 (%3ms)")
        .arg(result.size()).arg(model).arg(httpResp.latencyMs));

    return result;
}

std::vector<std::vector<float>> GeminiEmbeddingClient::generateEmbeddingsBatch(const std::vector<std::string>& texts, QString& outError, const QString& explicitApiKey)
{
    std::vector<std::vector<float>> allEmbeddings;
    if (texts.empty()) {
        return allEmbeddings;
    }

    QString apiKey = !explicitApiKey.isEmpty() ? explicitApiKey.trimmed() : GCloudConfig::instance().apiKey();
    if (apiKey.isEmpty()) {
        outError = "Gemini API key is required (pass via X-Gemini-API-Key header or api_key parameter)";
        return allEmbeddings;
    }

    QString model = GCloudConfig::instance().embeddingModelName();
    QUrl url = GeminiRestTransport::buildUrl(model, "batchEmbedContents", apiKey);

    // Google AI Studio supports up to 100 requests per batchEmbedContents call
    constexpr size_t kBatchChunkSize = 50;
    allEmbeddings.reserve(texts.size());

    for (size_t start = 0; start < texts.size(); start += kBatchChunkSize) {
        size_t end = std::min(start + kBatchChunkSize, texts.size());

        RequestPacer::instance().acquireEmbeddingSlot();

        QJsonArray requestsArr;
        for (size_t i = start; i < end; ++i) {
            QJsonObject contentObj;
            QJsonArray partsArr;
            QJsonObject partText;
            partText["text"] = QString::fromStdString(texts[i]);
            partsArr.append(partText);
            contentObj["parts"] = partsArr;

            QJsonObject reqItem;
            reqItem["model"] = QString("models/%1").arg(model);
            reqItem["content"] = contentObj;
            if (model.contains("embedding-004")) {
                reqItem["outputDimensionality"] = 384;
            }
            requestsArr.append(reqItem);
        }

        QJsonObject rootObj;
        rootObj["requests"] = requestsArr;

        auto httpResp = GeminiRestTransport::postJson(url, rootObj, 30000);
        if (!httpResp.success) {
            outError = httpResp.errorMessage;
            _log.error(outError);
            return allEmbeddings;
        }

        QJsonArray embList = httpResp.json.value("embeddings").toArray();
        for (const auto& embVal : embList) {
            QJsonArray vals = embVal.toObject().value("values").toArray();
            std::vector<float> vec;
            vec.reserve(vals.size());
            for (const auto& v : vals) {
                vec.push_back(static_cast<float>(v.toDouble()));
            }
            allEmbeddings.push_back(std::move(vec));
        }

        _log.info(QString("[Cloud] Generated batch of %1 embeddings via %2 in %3ms")
            .arg(embList.size()).arg(model).arg(httpResp.latencyMs));

        emit embeddingProgress(static_cast<int>(allEmbeddings.size()), static_cast<int>(texts.size()));
    }

    return allEmbeddings;
}


