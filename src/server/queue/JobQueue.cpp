#include "JobQueue.h"
#include "constants/CommonConstants.h"
#include "config/GCloudConfig.h"

// Qt
#include <QThreadPool>
#include <unordered_map>

JobQueue::JobQueue(Database &db, DocumentProcessor &proc, LlamaEngine &llama, VectorIndexRegistry &indexRegistry, QObject *parent)
    : QObject(parent), _db(db), _proc(proc), _llama(llama), _indexRegistry(indexRegistry)
{
    connect(&_timer, &QTimer::timeout, this, &JobQueue::pollNextJob);
}

void JobQueue::start(int pollIntervalMs)
{
    _timer.start(pollIntervalMs);
}

void JobQueue::notifyNewJob()
{
    pollNextJob();
}

void JobQueue::cancelJob(int jobId)
{
    if (_currentProcessingJobId.load() == jobId) {
        _log.warn(QString("Cancellation requested for active job %1").arg(jobId));
        _cancelRequested.store(true);
        QMutexLocker rLock(&_rateLimitMutex);
        _rateLimitResolution = RateLimitResolution::Cancel;
        _rateLimitCondition.wakeAll();
    }
}

void JobQueue::resolveRateLimit(RateLimitResolution resolution, const QString& newModel)
{
    QMutexLocker rLock(&_rateLimitMutex);
    _pendingNewModel = newModel;
    _rateLimitResolution = resolution;
    _isRateLimited.store(false);
    _rateLimitCondition.wakeAll();
}

void JobQueue::pollNextJob()
{
    if (_isProcessing.exchange(true))
        return;

    int jobId = -1;
    QVector<JobDocument> documents;
    if (!_db.getNextPendingJob(jobId, documents))
    {
        _isProcessing = false;
        return;
    }

    _currentProcessingJobId.store(jobId);
    _cancelRequested.store(false);

    _db.updateJobStatus(jobId, JobStatus::Processing);
    _log.info(QString("Job %1 PROCESSING: %2 document(s)").arg(jobId).arg(documents.size()));

    QThreadPool::globalInstance()->start([this, jobId, documents]()
    {
        bool inTx = false;
        try
        {
            if (_cancelRequested.load()) {
                throw std::runtime_error("Job cancelled by user");
            }

            bool isCloud = GCloudConfig::instance().isCloudEnabled();
            if (!isCloud) {
                if (!_llama.loadEmbdModel()) {
                    throw std::runtime_error("Failed to load local embedding model");
                }
            }

            QString embModelDesc = isCloud
                ? QString("Google Cloud (%1)").arg(GCloudConfig::instance().embeddingModelName())
                : "Local BGE (bge-small-en-v1.5)";
            _log.info(QString("Job %1: Generating vector embeddings via %2 for %3 document(s)...")
                .arg(jobId).arg(embModelDesc).arg(documents.size()));

            QString jobApiKey = _db.getJobApiKey(jobId);
            std::unordered_map<std::string, std::vector<float>> embeddingCache;
            std::vector<ChunkRecord> preparedChunks;
            bool restartEmbedding = true;

            while (restartEmbedding && !_cancelRequested.load()) {
                restartEmbedding = false;
                preparedChunks.clear();
                embeddingCache.clear();
                for (const auto& doc : documents) {
                    _db.updateDocumentStatus(doc.id, "QUEUED");
                }

                bool isCloud = GCloudConfig::instance().isCloudEnabled();
                if (!isCloud) {
                    if (!_llama.loadEmbdModel()) {
                        throw std::runtime_error("Failed to load local embedding model");
                    }
                }

                QString embModelDesc = isCloud
                    ? QString("Google Cloud (%1)").arg(GCloudConfig::instance().embeddingModelName())
                    : "Local BGE (bge-small-en-v1.5)";
                _log.info(QString("Job %1: Generating vector embeddings via %2 for %3 document(s)...")
                    .arg(jobId).arg(embModelDesc).arg(documents.size()));

                for (const JobDocument& document : documents) {
                    if (_cancelRequested.load() || restartEmbedding) {
                        break;
                    }

                    _db.updateDocumentStatus(document.id, "PROCESSING");
                    try {
                        auto chunks = _proc.processPdf(document.filePath.toStdString());
                        for (const auto& chunk : chunks) {
                            if (_cancelRequested.load() || restartEmbedding) {
                                break;
                            }

                            std::vector<float> emb;
                            auto it = embeddingCache.find(chunk.text);
                            if (it != embeddingCache.end()) {
                                emb = it->second;
                            } else {
                                if (isCloud) {
                                    QString embError;
                                    emb = _geminiEmbedding.generateEmbedding(chunk.text, embError, jobApiKey);
                                    while (emb.empty() && !_cancelRequested.load()) {
                                        _log.warn(QString("[RateLimit] Cloud embedding failed for job %1 (model %2): %3")
                                            .arg(jobId).arg(GCloudConfig::instance().embeddingModelName()).arg(embError));
                                        _rateLimitError = embError;
                                        _rateLimitedModel = GCloudConfig::instance().embeddingModelName();
                                        _isRateLimited.store(true);
                                        _db.updateJobStatus(jobId, "RATE_LIMITED");

                                        RateLimitResolution resolution = RateLimitResolution::None;
                                        {
                                            QMutexLocker rLock(&_rateLimitMutex);
                                            _rateLimitResolution = RateLimitResolution::None;
                                            while (_isRateLimited.load() && !_cancelRequested.load() && _rateLimitResolution == RateLimitResolution::None) {
                                                _rateLimitCondition.wait(&_rateLimitMutex, 1000);
                                            }
                                            _isRateLimited.store(false);
                                            resolution = _rateLimitResolution;
                                        }

                                        if (_cancelRequested.load() || resolution == RateLimitResolution::Cancel) {
                                            throw std::runtime_error("Job cancelled by user");
                                        }

                                        if (resolution == RateLimitResolution::SwitchToLocal) {
                                            _log.info(QString("Job %1: Switched to local engine. Discarding %2 partial chunk embeddings to avoid mixed vector spaces, restarting with BGE...")
                                                .arg(jobId).arg(preparedChunks.size()));
                                            GCloudConfig::instance().setCloudEnabled(false);
                                            _db.updateJobStatus(jobId, JobStatus::Processing);
                                            restartEmbedding = true;
                                            break;
                                        }

                                        if (resolution == RateLimitResolution::SwitchEmbeddingModel) {
                                            if (!_pendingNewModel.isEmpty()) {
                                                GCloudConfig::instance().setEmbeddingModelName(_pendingNewModel);
                                            }
                                            _log.info(QString("Job %1: Switched to model '%2'. Discarding %3 partial chunk embeddings to avoid mixed vector spaces, restarting...")
                                                .arg(jobId).arg(GCloudConfig::instance().embeddingModelName()).arg(preparedChunks.size()));
                                            _db.updateJobStatus(jobId, JobStatus::Processing);
                                            restartEmbedding = true;
                                            break;
                                        }

                                        if (resolution == RateLimitResolution::Retry) {
                                            _log.info(QString("Job %1: Retrying chunk embedding with model '%2'...")
                                                .arg(jobId).arg(GCloudConfig::instance().embeddingModelName()));
                                            _db.updateJobStatus(jobId, JobStatus::Processing);
                                            emb = _geminiEmbedding.generateEmbedding(chunk.text, embError, jobApiKey);
                                            continue;
                                        }
                                    }
                                } else {
                                    emb = _llama.generateEmbedding(chunk.text);
                                }

                                if (!emb.empty() && !restartEmbedding) {
                                    embeddingCache.emplace(chunk.text, emb);
                                }
                            }

                            if (!emb.empty() && !restartEmbedding) {
                                preparedChunks.push_back({
                                    document.id,
                                    chunk.pageNumber,
                                    QString::fromStdString(chunk.text),
                                    std::move(emb)
                                });
                            }
                        }

                        if (!restartEmbedding && !_cancelRequested.load()) {
                            _db.updateDocumentStatus(document.id, "INGESTED");
                        }
                    } catch (const std::exception& e) {
                        _db.updateDocumentStatus(document.id, "FAILED");
                        throw;
                    }
                }
            }

            // Keep embedding model resident in RAM (~67MB) for fast future ingestion & retrieval
            if (_cancelRequested.load()) {
                throw std::runtime_error("Job cancelled by user");
            }

            // Commit all prepared chunks and index vectors in a rapid, sub-millisecond transaction
            _db.transaction();
            inTx = true;
            QString modelProvenance = isCloud ? GCloudConfig::instance().embeddingModelName() : "bge-small-en-v1.5";
            QVector<int64_t> chunkIds = _db.insertChunksBatch(jobId, preparedChunks, modelProvenance);
            if (chunkIds.size() != static_cast<qsizetype>(preparedChunks.size()) || !_db.commit()) {
                throw std::runtime_error("Failed to commit chunk batch transaction");
            }
            inTx = false;

            _log.info(QString("Job %1: Persisted %2 chunk records and binary embedding BLOBs into SQLite table 'document_chunks' (provenance: %3)")
                .arg(jobId).arg(chunkIds.size()).arg(modelProvenance));

            // Bulk vectorized addition into FAISS index
            std::vector<int64_t> validIds;
            std::vector<float> flatEmbeddings;
            validIds.reserve(preparedChunks.size());
            flatEmbeddings.reserve(preparedChunks.size() * 384);

            for (size_t i = 0; i < preparedChunks.size(); ++i) {
                int64_t cid = chunkIds[static_cast<qsizetype>(i)];
                if (cid > 0 && !preparedChunks[i].embedding.empty()) {
                    validIds.push_back(cid);
                    flatEmbeddings.insert(flatEmbeddings.end(),
                                          preparedChunks[i].embedding.begin(),
                                          preparedChunks[i].embedding.end());
                }
            }

            if (!validIds.empty()) {
                auto index = _indexRegistry.getOrCreate(jobId);
                if (index) {
                    index->addBatch(validIds, flatEmbeddings);
                    _log.info(QString("Job %1: Indexed %2 vector embeddings into in-memory FAISS HNSW index")
                        .arg(jobId).arg(validIds.size()));
                }
            }

            _db.updateJobStatus(jobId, JobStatus::Completed);
            _log.info(QString("Job %1 COMPLETED (%2 chunks, %3 unique embeddings)")
                .arg(jobId).arg(preparedChunks.size()).arg(embeddingCache.size()));
            emit jobCompleted(jobId);
        }
        catch (const std::exception& e)
        {
            _llama.unloadEmbdModel();
            if (inTx) {
                _db.rollback();
            }
            if (_cancelRequested.load()) {
                _db.updateJobStatus(jobId, JobStatus::Cancelled, "Job cancelled by user");
                _log.warn(QString("Job %1 CANCELLED by user: in-flight embedding requests halted").arg(jobId));
            } else {
                _db.updateJobStatus(jobId, JobStatus::Failed, e.what());
                _log.error(QString("Job %1 FAILED: %2").arg(jobId).arg(e.what()));
            }
            for (const auto& doc : documents) {
                _db.updateDocumentStatus(doc.id, _cancelRequested.load() ? "CANCELLED" : "FAILED");
            }
        }

        _currentProcessingJobId.store(-1);
        _cancelRequested.store(false);
        _isProcessing = false;

        // Immediately check if there are further pending jobs in the queue
        pollNextJob();
    });
}
