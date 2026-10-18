#pragma once

#include <vector>
#include <string>
#include <QString>
#include <QObject>
#include "logging/Logger.h"

class GeminiEmbeddingClient : public QObject {
    Q_OBJECT

public:
    explicit GeminiEmbeddingClient(QObject* parent = nullptr);

    std::vector<float> generateEmbedding(const std::string& text, QString& outError, const QString& explicitApiKey = QString());
    std::vector<std::vector<float>> generateEmbeddingsBatch(const std::vector<std::string>& texts, QString& outError, const QString& explicitApiKey = QString());

signals:
    void embeddingProgress(int current, int total);

private:
    Logger _log{"GeminiEmbeddingClient"};
};

