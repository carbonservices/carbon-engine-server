#pragma once
#include <memory>
#include <unordered_map>
#include <QMutex>
#include "VectorIndex.h"

class Database;

// Owns one VectorIndex per ingestion job, so a RAG search for one job can never retrieve
// chunks that belong to a different job. Indices are created lazily and rebuilt on demand
// from SQLite persisted embeddings if the process restarted.
class VectorIndexRegistry {
public:
    explicit VectorIndexRegistry(int dimension, Database* db = nullptr);

    std::shared_ptr<VectorIndex> getOrCreate(int jobId);
    void remove(int jobId);

private:
    int _dimension;
    Database* _db{nullptr};
    QMutex _mutex;
    std::unordered_map<int, std::shared_ptr<VectorIndex>> _indicesByJobId;
};
