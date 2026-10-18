#ifndef DATABASE_H
#define DATABASE_H

#include <QString>
#include <QStringList>
#include <QSqlDatabase>
#include <QVector>
#include <QThread>
#include <QMutex>
#include <QHash>
#include <vector>
#include <functional>
#include "logging/Logger.h"

// One file staged within a job's batch, in the order it was submitted.
struct JobDocument {
    int id{0};
    QString filePath;
    QString status{"QUEUED"};
};

// Rich document metadata for jobs and report views
struct DocumentInfo {
    int id{0};
    int jobId{0};
    QString filePath;
    QString fileName;
    int chunkCount{0};
    int pageCount{0};
    QString status{"QUEUED"};
};

// One memoized fact / metric entry with provenance coordinates.
struct FactEntry {
    QString key;
    QString value;
    int64_t sourceChunkId{-1};
    int pageNumber{0};
};

// One field-group's extracted draft, as persisted in report_sections.
struct ReportSection {
    QString groupId;
    QString dataJson;
    QString status;
    QString sourceChunkIds;
};

// One report / job summary displayed in the UI history list.
struct ReportSummary {
    int jobId;
    QString title;
    QString status;
    QString extractionStatus;
    QString createdAt;
    QStringList documents;
};

// One chunk record prepared for batch database persistence.
struct ChunkRecord {
    int documentId;
    int pageNumber;
    QString text;
    std::vector<float> embedding;
};

class Database {
public:
    explicit Database(const QString& dbPath);
    ~Database();

    bool init();
    bool reconcileStuckJobs();

    // Transaction helpers for bulk insertions
    bool transaction();
    bool commit();
    bool rollback();

    int enqueueJob(const QStringList& filePaths, const QString& apiKey = "");
    QString getJobApiKey(int jobId);
    bool setJobApiKey(int jobId, const QString& apiKey);
    bool getNextPendingJob(int& outJobId, QVector<JobDocument>& outDocuments);
    bool updateJobStatus(int jobId, const QString& status, const QString& error = "");
    bool updateDocumentStatus(int documentId, const QString& status);
    bool updateDocumentStatsAndStatus(int documentId, int chunkCount, int pageCount, const QString& status);
    
    // Phase 3 & 5: insertChunk persisting embeddings as BLOBs, returning chunk ID
    int64_t insertChunk(int jobId, int documentId, int pageNum, const QString& chunkText,
                        const std::vector<float>& embedding, const QString& embeddingModel = "bge-small-en-v1.5");
    QVector<int64_t> insertChunksBatch(int jobId, const std::vector<ChunkRecord>& chunks,
                                       const QString& embeddingModel = "bge-small-en-v1.5");
    
    bool loadEmbeddingsForJob(int jobId, const std::function<void(int64_t id, const std::vector<float>& vec)>& consumer);
    QMap<int64_t, QString> getChunksByIds(const std::vector<int64_t>& ids);
    QVector<QString> getChunksForJob(int jobId);
    QVector<int64_t> getChunkIdsOnPage(int jobId, int pageNumber);

    // Provenance & FactStore persistence
    bool upsertFact(int jobId, const QString& key, const QString& value, int64_t chunkId = -1, int pageNumber = 0);
    QHash<QString, FactEntry> getFactsForJob(int jobId);

    // Real system & storage telemetry
    quint64 diskSizeBytes() const;
    int totalChunkCount();

    QString getJobStatus(int jobId);
    int getPendingJobCount();
    bool deleteJob(int jobId);
    QVector<ReportSummary> getAllReports();
    QVector<DocumentInfo> getJobDocuments(int jobId);

    bool setExtractionStatus(int jobId, const QString& status);
    QString getExtractionStatus(int jobId);
    bool upsertReportSection(int jobId, const QString& groupId, const QString& dataJson,
                             const QString& status = "DRAFT", const QString& sourceChunkIds = "");
    QVector<ReportSection> getReportSections(int jobId);
    bool lockReport(int jobId);
    bool isReportLocked(int jobId);

private:
    QString _dbPath;
    Logger _log{"Database"};
    bool runMigrations();
    
    // Helper to guarantee a valid connection for the calling thread
    QSqlDatabase getDb(); 
};

#endif // DATABASE_H
