#include "GeminiClient.h"
#include "GeminiRestTransport.h"
#include "../config/GCloudConfig.h"
#include "../rate_limiter/RequestPacer.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

GeminiClient::GeminiClient(QObject* parent)
    : QObject(parent)
{
}

GeminiGenerationResult GeminiClient::generateExtraction(const QString& instructions, const QString& context,
                                                         const QString& explicitApiKey)
{
    GeminiGenerationResult result;
    QString apiKey = !explicitApiKey.isEmpty() ? explicitApiKey.trimmed() : GCloudConfig::instance().apiKey();
    if (apiKey.isEmpty()) {
        result.success = false;
        result.error = "Gemini API key is required (pass via X-Gemini-API-Key header or api_key parameter)";
        return result;
    }

    RequestPacer::instance().acquireGenerationSlot();

    QString model = GCloudConfig::instance().modelName();
    QUrl url = GeminiRestTransport::buildUrl(model, "generateContent", apiKey);

    QJsonObject rootObj;

    // System instruction
    QJsonObject sysInstObj;
    QJsonArray sysParts;
    QJsonObject sysTextPart;
    sysTextPart["text"] = instructions;
    sysParts.append(sysTextPart);
    sysInstObj["parts"] = sysParts;
    rootObj["system_instruction"] = sysInstObj;

    // User content
    QJsonArray contentsArr;
    QJsonObject userMsgObj;
    userMsgObj["role"] = "user";
    QJsonArray userParts;
    QJsonObject userTextPart;
    userTextPart["text"] = context;
    userParts.append(userTextPart);
    userMsgObj["parts"] = userParts;
    contentsArr.append(userMsgObj);
    rootObj["contents"] = contentsArr;

    // Generation config (Strict JSON)
    QJsonObject genConfigObj;
    genConfigObj["temperature"] = 0.0;
    genConfigObj["responseMimeType"] = "application/json";
    rootObj["generationConfig"] = genConfigObj;

    auto httpResp = GeminiRestTransport::postJson(url, rootObj, 60000);
    result.latencyMs = httpResp.latencyMs;

    if (!httpResp.success) {
        result.success = false;
        result.error = httpResp.errorMessage;
        _log.error(result.error);
        return result;
    }

    QJsonObject respObj = httpResp.json;

    // Parse token usage metadata
    if (respObj.contains("usageMetadata")) {
        QJsonObject usage = respObj.value("usageMetadata").toObject();
        result.promptTokens = usage.value("promptTokenCount").toInt();
        result.candidateTokens = usage.value("candidatesTokenCount").toInt();
    }

    // Extract generated text
    QJsonArray candidates = respObj.value("candidates").toArray();
    if (candidates.isEmpty()) {
        result.success = false;
        result.error = "Gemini returned empty candidates array";
        return result;
    }

    QJsonObject firstCandidate = candidates.first().toObject();
    QJsonObject content = firstCandidate.value("content").toObject();
    QJsonArray parts = content.value("parts").toArray();
    if (parts.isEmpty()) {
        result.success = false;
        result.error = "Gemini candidate returned empty content parts";
        return result;
    }

    result.jsonResponse = parts.first().toObject().value("text").toString();
    result.success = true;

    _lastLatencyMs.store(result.latencyMs);
    _lastPromptTokens.store(result.promptTokens);
    _lastCandidateTokens.store(result.candidateTokens);

    return result;
}

QStringList GeminiClient::fetchAvailableModels(const QString& apiKey, QString& outError)
{
    QString key = !apiKey.isEmpty() ? apiKey : GCloudConfig::instance().apiKey();
    if (key.isEmpty()) {
        outError = "Gemini API key is required (pass via X-Gemini-API-Key header or api_key parameter)";
        return {};
    }

    QUrl url(QString("https://generativelanguage.googleapis.com/v1beta/models?key=%1").arg(key));
    auto resp = GeminiRestTransport::getJson(url, 20000);
    if (!resp.success) {
        outError = resp.errorMessage;
        return {};
    }

    QJsonArray modelsArr = resp.json.value("models").toArray();
    QStringList genModels;
    QStringList embedModels;
    for (const auto& item : modelsArr) {
        QJsonObject mObj = item.toObject();
        QJsonArray methods = mObj.value("supportedGenerationMethods").toArray();
        bool canGen = false;
        bool canEmbed = false;
        for (const auto& meth : methods) {
            QString mStr = meth.toString();
            if (mStr == "generateContent") {
                canGen = true;
            }
            if (mStr == "embedContent") {
                canEmbed = true;
            }
        }
        QString name = mObj.value("name").toString();
        if (name.startsWith("models/")) {
            name = name.mid(7);
        }
        if (canGen) {
            genModels.append(name);
        }
        if (canEmbed) {
            embedModels.append(name);
        }
    }

    if (!genModels.isEmpty()) {
        GCloudConfig::instance().setAvailableModels(genModels);
    }
    if (!embedModels.isEmpty()) {
        GCloudConfig::instance().setAvailableEmbeddingModels(embedModels);
    }
    return genModels;
}

bool GeminiClient::testConnection(QString& outModel, qint64& outLatencyMs, QString& outError,
                                  const QString& overrideApiKey, const QString& overrideModel)
{
    QString apiKey = !overrideApiKey.isEmpty() ? overrideApiKey : GCloudConfig::instance().apiKey();
    if (apiKey.isEmpty()) {
        outError = "Gemini API key is required (pass via X-Gemini-API-Key header or api_key parameter)";
        return false;
    }

    // Query live Gemini models from the API
    QString listError;
    QStringList liveModels = fetchAvailableModels(apiKey, listError);

    outModel = !overrideModel.isEmpty() ? overrideModel : GCloudConfig::instance().modelName();

    // If outModel is not supported in live models or empty, select the best model from API
    if (!liveModels.isEmpty() && (!liveModels.contains(outModel) || outModel.isEmpty())) {
        QString bestModel = liveModels.first();
        for (const auto& m : liveModels) {
            if (m.contains("flash", Qt::CaseInsensitive)) {
                bestModel = m;
                break;
            }
        }
        outModel = bestModel;
        GCloudConfig::instance().setModelName(outModel);
    }

    RequestPacer::instance().acquireGenerationSlot();
    QUrl url = GeminiRestTransport::buildUrl(outModel, "generateContent", apiKey);

    QJsonObject rootObj;
    QJsonObject userMsgObj;
    userMsgObj["role"] = "user";
    QJsonArray userParts;
    QJsonObject userTextPart;
    userTextPart["text"] = "Ping test.";
    userParts.append(userTextPart);
    userMsgObj["parts"] = userParts;

    QJsonArray contentsArr;
    contentsArr.append(userMsgObj);
    rootObj["contents"] = contentsArr;

    QJsonObject genConfigObj;
    genConfigObj["temperature"] = 0.0;
    genConfigObj["maxOutputTokens"] = 16;
    rootObj["generationConfig"] = genConfigObj;

    auto httpResp = GeminiRestTransport::postJson(url, rootObj, 20000);
    outLatencyMs = httpResp.latencyMs;
    if (!httpResp.success) {
        outError = httpResp.errorMessage;
        return false;
    }

    return true;
}

bool GeminiClient::testEmbeddingConnection(QString& outEmbeddingModel, qint64& outLatencyMs, int& outDim, QString& outError,
                                          const QString& overrideApiKey, const QString& overrideEmbeddingModel)
{
    QString apiKey = !overrideApiKey.isEmpty() ? overrideApiKey : GCloudConfig::instance().apiKey();
    if (apiKey.isEmpty()) {
        outError = "Gemini API key is required (pass via X-Gemini-API-Key header or api_key parameter)";
        return false;
    }

    outEmbeddingModel = !overrideEmbeddingModel.isEmpty() ? overrideEmbeddingModel : GCloudConfig::instance().embeddingModelName();
    if (outEmbeddingModel.isEmpty()) {
        outEmbeddingModel = "text-embedding-004";
    }

    RequestPacer::instance().acquireEmbeddingSlot();
    QUrl url = GeminiRestTransport::buildUrl(outEmbeddingModel, "embedContent", apiKey);

    QJsonObject contentObj;
    QJsonArray partsArr;
    QJsonObject partText;
    partText["text"] = "Embedding connectivity ping test.";
    partsArr.append(partText);
    contentObj["parts"] = partsArr;

    QJsonObject rootObj;
    rootObj["model"] = QString("models/%1").arg(outEmbeddingModel);
    rootObj["content"] = contentObj;
    if (outEmbeddingModel.contains("embedding-004")) {
        rootObj["outputDimensionality"] = 384;
    }

    auto httpResp = GeminiRestTransport::postJson(url, rootObj, 20000);
    outLatencyMs = httpResp.latencyMs;
    if (!httpResp.success) {
        outError = httpResp.errorMessage;
        return false;
    }

    QJsonObject embObj = httpResp.json.value("embedding").toObject();
    QJsonArray vals = embObj.value("values").toArray();
    outDim = vals.size();
    if (outDim == 0) {
        outError = "API returned an empty vector array";
        return false;
    }

    return true;
}
