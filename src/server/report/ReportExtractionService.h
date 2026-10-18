#pragma once

#include <atomic>
#include <QElapsedTimer>
#include <QString>
#include <QVector>
#include <QMutex>

#include "constants/CommonConstants.h"
#include "report/BRSRSchema.h"
#include "report/FactStore.h"
#include "db/Database.h"
#include "inference/LlamaEngine.h"
#include "client/GeminiClient.h"
#include "embedding/GeminiEmbeddingClient.h"
#include "logging/Logger.h"
#include "system/SystemMonitor.h"
#include "vector/VectorIndexRegistry.h"
#include <QWaitCondition>

class ReportExtractionService {
public:
    enum class RateLimitResolution {
        None,
        SwitchCloudModel,
        SwitchToLocal,
        Retry,
        Cancel
    };

    struct MissingSection {
        QString groupId;
        QString groupLabel;
        QStringList missingKeys;
    };

    struct ProgressInfo {
        int currentStep{0};
        int totalSteps{0};
        QString currentGroupId;
        QString currentGroupLabel;
        QVector<MissingSection> missingSections;
        QString engineMode{EngineMode::Offline};
        QString activeModel;
        qint64 lastLatencyMs{0};
        QString pacingStatus;
        bool isRateLimited{false};
        QString rateLimitError;
        QString rateLimitedModel;
        QString rateLimitedSection;
        QString rateLimitType{"INFERENCE"};
    };

    ReportExtractionService(Database& db, LlamaEngine& llama, GeminiClient& gemini,
                            VectorIndexRegistry& indexRegistry,
                            const BRSRSchemaCatalog& catalog, SystemMonitor& monitor);

    void extractJob(int jobId, const QString& explicitApiKey = QString());

    bool isExtracting() const { return _isExtracting.load(); }
    int activeJobId() const { return _activeJobId.load(); }
    void cancelJob(int jobId);

    void resolveRateLimit(RateLimitResolution resolution, const QString& newModel = QString());
    bool isRateLimited() const { return _isRateLimited.load(); }

    FactStore& factStore() { return _factStore; }
    ProgressInfo currentProgress() const;

private:
    struct GroupContext {
        QString groupId;
        QString assembledContext;
        QVector<int64_t> sourceChunkIds;
        qint64 retrievalMs{0};
    };

    GroupContext retrieveContextForGroup(int jobId, const FieldGroup& group);
    std::string buildGemmaPrompt(int jobId, const FieldGroup& group, const QString& context) const;
    QString buildInstructions(int jobId, const FieldGroup& group) const;

    void generateAndPersistGroup(int jobId, const FieldGroup& group, const GroupContext& ctx,
                                 qint64& outPrefillMs, qint64& outDecodeMs, int& outTokensGenerated,
                                 const QString& explicitApiKey = QString());

    Database& _db;
    LlamaEngine& _llama;
    GeminiClient& _gemini;
    GeminiEmbeddingClient _geminiEmbedding;
    VectorIndexRegistry& _indexRegistry;
    const BRSRSchemaCatalog& _catalog;
    SystemMonitor& _monitor;
    FactStore _factStore;

    std::atomic<bool> _isExtracting{false};
    std::atomic<int> _activeJobId{-1};
    std::atomic<bool> _cancelRequested{false};

    std::atomic<bool> _isRateLimited{false};
    mutable QMutex _rateLimitMutex;
    QWaitCondition _rateLimitCondition;
    RateLimitResolution _rateLimitResolution{RateLimitResolution::None};
    QString _pendingNewModel;

    mutable QMutex _progressMutex;
    ProgressInfo _progress;
    Logger _log{"ReportExtractionService"};
};
