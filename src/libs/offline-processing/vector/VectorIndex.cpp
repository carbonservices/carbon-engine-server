#include "VectorIndex.h"
#include <QMutexLocker>

VectorIndex::VectorIndex(int dimension)
    : _dim(dimension), _hnsw(dimension, 32), _index(&_hnsw) {}

void VectorIndex::add(int64_t id, const std::vector<float>& embedding) {
    QMutexLocker locker(&_mutex);
    faiss::idx_t idxId = id;
    _index.add_with_ids(1, embedding.data(), &idxId);
}

void VectorIndex::addBatch(const std::vector<int64_t>& ids, const std::vector<float>& embeddings) {
    if (ids.empty() || embeddings.empty()) return;
    QMutexLocker locker(&_mutex);
    std::vector<faiss::idx_t> faissIds(ids.begin(), ids.end());
    _index.add_with_ids(static_cast<faiss::idx_t>(ids.size()), embeddings.data(), faissIds.data());
}

std::vector<int64_t> VectorIndex::search(const std::vector<float>& query, int k) {
    if (k <= 0) return {};

    QMutexLocker locker(&_mutex);
    std::vector<float> distances(k);
    std::vector<faiss::idx_t> indices(k);

    _index.search(1, query.data(), k, distances.data(), indices.data());

    std::vector<int64_t> results;
    for (int i = 0; i < k; ++i) {
        if (indices[i] != -1) {
            results.push_back(indices[i]);
        }
    }
    return results;
}

int VectorIndex::count() const {
    QMutexLocker locker(&const_cast<QMutex&>(_mutex));
    return static_cast<int>(_index.ntotal);
}
