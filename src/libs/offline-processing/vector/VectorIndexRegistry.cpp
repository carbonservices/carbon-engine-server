#include "VectorIndexRegistry.h"
#include "db/Database.h"

VectorIndexRegistry::VectorIndexRegistry(int dimension, Database* db)
    : _dimension(dimension), _db(db) {}

std::shared_ptr<VectorIndex> VectorIndexRegistry::getOrCreate(int jobId)
{
    QMutexLocker locker(&_mutex);
    auto it = _indicesByJobId.find(jobId);
    if (it == _indicesByJobId.end()) {
        auto index = std::make_shared<VectorIndex>(_dimension);
        if (_db) {
            _db->loadEmbeddingsForJob(jobId, [&index](int64_t id, const std::vector<float>& vec) {
                index->add(id, vec);
            });
        }
        it = _indicesByJobId.emplace(jobId, std::move(index)).first;
    }
    return it->second;
}

void VectorIndexRegistry::remove(int jobId)
{
    QMutexLocker locker(&_mutex);
    _indicesByJobId.erase(jobId);
}
