#ifndef JOB_QUEUE_H
#define JOB_QUEUE_H

#include "db/Database.h"
#include "ingestion/DocumentProcessor.h"
#include "inference/LlamaEngine.h"
#include "embedding/GeminiEmbeddingClient.h"
#include "logging/Logger.h"
#include "vector/VectorIndexRegistry.h"

#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>

class JobQueue : public QObject
{
    Q_OBJECT
public:
    enum class RateLimitResolution {
        None,
        SwitchEmbeddingModel,
        SwitchToLocal,
        Retry,
        Cancel
    };

    explicit JobQueue(Database& db, DocumentProcessor& proc, LlamaEngine& llama, VectorIndexRegistry& indexRegistry, QObject* parent = Q_NULLPTR);
    void start(int pollIntervalMs = 2000);

    void notifyNewJob();
    void cancelJob(int jobId);

    bool isProcessing() const { return _isProcessing.load(); }
    int currentJobId() const { return _currentProcessingJobId.load(); }
    bool isRateLimited() const { return _isRateLimited.load(); }
    QString rateLimitError() const { return _rateLimitError; }
    QString rateLimitedModel() const { return _rateLimitedModel; }

    void resolveRateLimit(RateLimitResolution resolution, const QString& newModel = QString());

signals:
    void jobCompleted(int jobId);

public slots:
    void pollNextJob();

private:
    Database& _db;
    DocumentProcessor& _proc;
    LlamaEngine& _llama;
    GeminiEmbeddingClient _geminiEmbedding;
    VectorIndexRegistry& _indexRegistry;
    QTimer _timer;
    std::atomic<bool> _isProcessing{false};
    std::atomic<int> _currentProcessingJobId{-1};
    std::atomic<bool> _cancelRequested{false};

    std::atomic<bool> _isRateLimited{false};
    QString _rateLimitError;
    QString _rateLimitedModel;
    mutable QMutex _rateLimitMutex;
    QWaitCondition _rateLimitCondition;
    RateLimitResolution _rateLimitResolution{RateLimitResolution::None};
    QString _pendingNewModel;

    Logger _log{"JobQueue"};
};

#endif // JOB_QUEUE_H