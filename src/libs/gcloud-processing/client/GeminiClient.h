#pragma once

#include <QString>
#include <QObject>
#include <atomic>
#include "logging/Logger.h"

struct GeminiGenerationResult {
    QString jsonResponse;
    qint64 latencyMs{0};
    int promptTokens{0};
    int candidateTokens{0};
    bool success{false};
    QString error;
};

class GeminiClient : public QObject {
    Q_OBJECT

public:
    explicit GeminiClient(QObject* parent = nullptr);

    GeminiGenerationResult generateExtraction(const QString& instructions, const QString& context,
                                              const QString& explicitApiKey = QString());
    QStringList fetchAvailableModels(const QString& apiKey, QString& outError);
    bool testConnection(QString& outModel, qint64& outLatencyMs, QString& outError,
                        const QString& overrideApiKey = QString(),
                        const QString& overrideModel = QString());
    bool testEmbeddingConnection(QString& outEmbeddingModel, qint64& outLatencyMs, int& outDim, QString& outError,
                                 const QString& overrideApiKey = QString(),
                                 const QString& overrideEmbeddingModel = QString());

    qint64 lastLatencyMs() const { return _lastLatencyMs.load(); }
    int lastPromptTokens() const { return _lastPromptTokens.load(); }
    int lastCandidateTokens() const { return _lastCandidateTokens.load(); }

private:
    Logger _log{"GeminiClient"};
    std::atomic<qint64> _lastLatencyMs{0};
    std::atomic<int> _lastPromptTokens{0};
    std::atomic<int> _lastCandidateTokens{0};
};
