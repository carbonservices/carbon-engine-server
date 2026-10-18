#include "FactStore.h"
#include <QMutexLocker>

FactStore::FactStore(Database* db) : _db(db) {}

void FactStore::loadForJob(int jobId)
{
    if (!_db) return;
    QMutexLocker locker(&_mutex);
    _factsByJob[jobId] = _db->getFactsForJob(jobId);
}

void FactStore::put(int jobId, const QString& key, const QString& value, int64_t chunkId, int pageNumber)
{
    if (key.isEmpty() || value.isEmpty() || value == "null" || value == "N/A") {
        return;
    }

    {
        QMutexLocker locker(&_mutex);
        FactEntry entry{key, value, chunkId, pageNumber};
        _factsByJob[jobId].insert(key, entry);
    }

    if (_db) {
        _db->upsertFact(jobId, key, value, chunkId, pageNumber);
    }
}

bool FactStore::get(int jobId, const QString& key, FactEntry& out) const
{
    QMutexLocker locker(&_mutex);
    auto jobIt = _factsByJob.find(jobId);
    if (jobIt == _factsByJob.end()) return false;

    auto it = jobIt.value().find(key);
    if (it == jobIt.value().end()) return false;

    out = it.value();
    return true;
}

QHash<QString, FactEntry> FactStore::getAllForJob(int jobId) const
{
    QMutexLocker locker(&_mutex);
    return _factsByJob.value(jobId);
}

void FactStore::clearForJob(int jobId)
{
    QMutexLocker locker(&_mutex);
    _factsByJob.remove(jobId);
    _anchorsByJob.remove(jobId);
}

void FactStore::recordTopicAnchor(int jobId, const QString& topic, int64_t chunkId, int /*pageNumber*/)
{
    if (chunkId <= 0 || topic.isEmpty()) return;
    QMutexLocker locker(&_mutex);
    auto& list = _anchorsByJob[jobId][topic];
    if (!list.contains(chunkId)) {
        list.append(chunkId);
    }
}

QVector<int64_t> FactStore::getAnchorChunksForTopic(int jobId, const QString& topic) const
{
    QMutexLocker locker(&_mutex);
    auto jobIt = _anchorsByJob.find(jobId);
    if (jobIt == _anchorsByJob.end()) return {};
    return jobIt.value().value(topic);
}

QString FactStore::formatKnownFactsForPrompt(int jobId) const
{
    QMutexLocker locker(&_mutex);
    auto jobIt = _factsByJob.find(jobId);
    if (jobIt == _factsByJob.end() || jobIt.value().isEmpty()) {
        return "";
    }

    const auto& facts = jobIt.value();
    QStringList lines;

    auto appendFact = [&](const QString& label, const QString& key) {
        if (facts.contains(key) && !facts[key].value.isEmpty()) {
            lines.append(QString("- %1: %2").arg(label, facts[key].value));
        }
    };

    appendFact("Reporting Entity", "entity_name");
    appendFact("Current Financial Year", "current_fy");
    appendFact("Previous Financial Year", "previous_fy");
    appendFact("Reporting Financial Year", "reporting_financial_year");
    appendFact("Total Turnover", "csr_turnover_amount");
    appendFact("Total Permanent Employees", "emp_perm_total");
    appendFact("Total Employees", "emp_total_total");

    if (lines.isEmpty()) {
        return "";
    }

    return "Known Verified Document Facts (Ground Truth anchors for context):\n" + lines.join("\n") + "\n\n";
}

