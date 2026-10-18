#pragma once

#include <QHash>
#include <QMutex>
#include <QString>
#include <QVector>
#include "db/Database.h"

// In-memory thread-safe cache for accumulating extracted metrics and tracking topic coordinates.
// Replaces ChunkRelevanceTracker and grounds downstream field prompts with proven facts.
class FactStore {
public:
    explicit FactStore(Database* db = nullptr);

    // Loads any previously persisted facts for this job from SQLite.
    void loadForJob(int jobId);

    // Records a verified fact, persisting it to SQLite if Database* is present.
    void put(int jobId, const QString& key, const QString& value, int64_t chunkId = -1, int pageNumber = 0);

    // Retrieves a specific fact if present.
    bool get(int jobId, const QString& key, FactEntry& out) const;

    // Returns all known facts for the job.
    QHash<QString, FactEntry> getAllForJob(int jobId) const;

    // Removes all in-memory facts and topic anchors for this job to free memory.
    void clearForJob(int jobId);

    // Associates a high-level topic (e.g. "P6_ENVIRONMENT", "P3_EMPLOYEES") with a proven chunk ID & page.
    void recordTopicAnchor(int jobId, const QString& topic, int64_t chunkId, int pageNumber);

    // Returns anchor chunk IDs previously recorded for this topic.
    QVector<int64_t> getAnchorChunksForTopic(int jobId, const QString& topic) const;

    // Formats known base facts (Company Name, Current FY, Previous FY, Turnover, Headcount)
    // into a prompt context header for LLM grounding.
    QString formatKnownFactsForPrompt(int jobId) const;

private:
    Database* _db{nullptr};
    mutable QMutex _mutex;
    // Map: jobId -> (metricKey -> FactEntry)
    QHash<int, QHash<QString, FactEntry>> _factsByJob;
    // Map: jobId -> (topic -> list of verified chunkIds)
    QHash<int, QHash<QString, QVector<int64_t>>> _anchorsByJob;
};

