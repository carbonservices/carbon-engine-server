#pragma once

#include <QString>
#include <QStringList>
#include <QMutex>
#include <QObject>

#include "constants/CommonConstants.h"

class GCloudConfig : public QObject {
    Q_OBJECT

public:
    static GCloudConfig& instance();

    QString apiKey() const;
    void setApiKey(const QString& key);

    QString modelName() const;
    void setModelName(const QString& model);

    QString embeddingModelName() const;
    void setEmbeddingModelName(const QString& model);

    bool isCloudEnabled() const;
    void setCloudEnabled(bool enabled);

    QStringList availableModels() const;
    void setAvailableModels(const QStringList& models);

    QStringList availableEmbeddingModels() const;
    void setAvailableEmbeddingModels(const QStringList& models);

    QString dataProcessingDisclaimer() const;

signals:
    void configChanged();

private:
    explicit GCloudConfig(QObject* parent = nullptr);

    mutable QMutex _mutex;
    QString _apiKey;
    QString _modelName{Models::CloudDefaultModel};
    QString _embeddingModelName{Models::CloudDefaultEmbeddingModel};
    QStringList _availableModels;
    QStringList _availableEmbeddingModels;
    bool _cloudEnabled{false};
};

