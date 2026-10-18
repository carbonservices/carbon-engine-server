#include "GCloudConfig.h"
#include <QMutexLocker>
#include <QProcessEnvironment>
#include <QSettings>

namespace {
inline constexpr const char* kOrgName = "CarbonServices";
inline constexpr const char* kAppName = "CarbonEngine";
inline constexpr const char* kKeyApiKey = "gcloud/api_key";
inline constexpr const char* kKeyModelName = "gcloud/model_name";
inline constexpr const char* kKeyEmbeddingModel = "gcloud/embedding_model";
inline constexpr const char* kKeyEnabled = "gcloud/enabled";

void saveSetting(const char* key, const QVariant& value)
{
    QSettings settings(kOrgName, kAppName);
    settings.setValue(key, value);
}
} // namespace

GCloudConfig& GCloudConfig::instance()
{
    static GCloudConfig s_instance;
    return s_instance;
}

GCloudConfig::GCloudConfig(QObject* parent)
    : QObject(parent)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString envKey = env.value("GEMINI_API_KEY", "");
    QString envModel = env.value("CARBON_ENGINE_CLOUD_MODEL", Models::CloudDefaultModel);
    bool envEnabled = env.value("CARBON_ENGINE_CLOUD_ENABLED", "0") == "1";

    QSettings settings(kOrgName, kAppName);
    _apiKey = settings.value(kKeyApiKey, envKey).toString();
    _modelName = settings.value(kKeyModelName, envModel).toString();
    _embeddingModelName = settings.value(kKeyEmbeddingModel, Models::CloudDefaultEmbeddingModel).toString();
    _availableModels = settings.value("gcloud/available_models").toStringList();
    _availableEmbeddingModels = settings.value("gcloud/available_embedding_models").toStringList();
    _cloudEnabled = settings.value(kKeyEnabled, envEnabled).toBool();
}

QString GCloudConfig::apiKey() const
{
    QMutexLocker locker(&_mutex);
    return _apiKey;
}

void GCloudConfig::setApiKey(const QString& key)
{
    {
        QMutexLocker locker(&_mutex);
        _apiKey = key.trimmed();
        saveSetting(kKeyApiKey, _apiKey);
    }
    emit configChanged();
}

QString GCloudConfig::modelName() const
{
    QMutexLocker locker(&_mutex);
    return _modelName;
}

void GCloudConfig::setModelName(const QString& model)
{
    {
        QMutexLocker locker(&_mutex);
        _modelName = model.trimmed();
        saveSetting(kKeyModelName, _modelName);
    }
    emit configChanged();
}

QString GCloudConfig::embeddingModelName() const
{
    QMutexLocker locker(&_mutex);
    return _embeddingModelName;
}

void GCloudConfig::setEmbeddingModelName(const QString& model)
{
    {
        QMutexLocker locker(&_mutex);
        _embeddingModelName = model.trimmed();
        saveSetting(kKeyEmbeddingModel, _embeddingModelName);
    }
    emit configChanged();
}

bool GCloudConfig::isCloudEnabled() const
{
    QMutexLocker locker(&_mutex);
    return _cloudEnabled;
}

void GCloudConfig::setCloudEnabled(bool enabled)
{
    {
        QMutexLocker locker(&_mutex);
        _cloudEnabled = enabled;
        saveSetting(kKeyEnabled, _cloudEnabled);
    }
    emit configChanged();
}

QStringList GCloudConfig::availableModels() const
{
    QMutexLocker locker(&_mutex);
    if (!_availableModels.isEmpty()) {
        return _availableModels;
    }
    return { Models::CloudDefaultModel };
}

void GCloudConfig::setAvailableModels(const QStringList& models)
{
    {
        QMutexLocker locker(&_mutex);
        _availableModels = models;
        saveSetting("gcloud/available_models", _availableModels);
    }
    emit configChanged();
}

QStringList GCloudConfig::availableEmbeddingModels() const
{
    QMutexLocker locker(&_mutex);
    if (!_availableEmbeddingModels.isEmpty()) {
        return _availableEmbeddingModels;
    }
    return { "text-embedding-004", "gemini-embedding-001", "gemini-embedding-2-preview" };
}

void GCloudConfig::setAvailableEmbeddingModels(const QStringList& models)
{
    {
        QMutexLocker locker(&_mutex);
        _availableEmbeddingModels = models;
        saveSetting("gcloud/available_embedding_models", _availableEmbeddingModels);
    }
    emit configChanged();
}

QString GCloudConfig::dataProcessingDisclaimer() const
{
    return "Note: Prompts and generated content processed via Google AI Studio Free Tier may be reviewed "
           "by human evaluators and used to train Google products. Do not submit sensitive corporate or "
           "Unpublished Price Sensitive Information (UPSI) without an enterprise Vertex AI configuration.";
}

