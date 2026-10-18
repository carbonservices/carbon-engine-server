#pragma once
#include <vector>
#include <QMutex>
#include <faiss/IndexHNSW.h>
#include <faiss/IndexIDMap.h>

class VectorIndex {
public:
    explicit VectorIndex(int dimension);
    ~VectorIndex() = default;

    void add(int64_t id, const std::vector<float>& embedding);
    void addBatch(const std::vector<int64_t>& ids, const std::vector<float>& embeddings);
    std::vector<int64_t> search(const std::vector<float>& query, int k);
    int count() const;

private:
    int _dim;
    QMutex _mutex;
    faiss::IndexHNSWFlat _hnsw;
    faiss::IndexIDMap _index;
};
