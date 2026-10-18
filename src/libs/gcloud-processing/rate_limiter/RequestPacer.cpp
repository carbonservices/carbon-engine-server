#include "RequestPacer.h"
#include <QMutexLocker>
#include <QThread>
#include <algorithm>

RequestPacer& RequestPacer::instance()
{
    static RequestPacer s_instance;
    return s_instance;
}

RequestPacer::RequestPacer()
{
    auto now = std::chrono::steady_clock::now();
    _nextGenTime = now;
    _nextEmbdTime = now;
}

void RequestPacer::acquireSlot(std::chrono::steady_clock::time_point& nextSlotTime, qint64 minIntervalMs, const QString& slotLabel)
{
    qint64 waitMs = 0;
    {
        QMutexLocker locker(&_mutex);
        auto now = std::chrono::steady_clock::now();
        if (nextSlotTime < now) {
            nextSlotTime = now;
        }
        auto scheduledTime = nextSlotTime;
        nextSlotTime = scheduledTime + std::chrono::milliseconds(minIntervalMs);

        auto waitDuration = scheduledTime - now;
        waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(waitDuration).count();
        _lastWaitMs = (waitMs > 0) ? waitMs : 0;
        _statusMsg = (waitMs > 0)
            ? QString("Pacing %1 (%2 ms)").arg(slotLabel).arg(waitMs)
            : QString("%1 slot ready").arg(slotLabel);
    }

    if (waitMs > 0) {
        QThread::msleep(static_cast<unsigned long>(waitMs));
    }
}

void RequestPacer::acquireGenerationSlot()
{
    acquireSlot(_nextGenTime, _minGenIntervalMs, "generation");
}

void RequestPacer::acquireEmbeddingSlot()
{
    acquireSlot(_nextEmbdTime, _minEmbdIntervalMs, "embedding");
}

void RequestPacer::reportRateLimitError()
{
    QMutexLocker locker(&_mutex);
    _statusMsg = "Rate limit reached (Free Tier quota exceeded)";
}

void RequestPacer::reportSuccess()
{
    QMutexLocker locker(&_mutex);
    _statusMsg = "Ready";
}

qint64 RequestPacer::lastPacingWaitMs() const
{
    QMutexLocker locker(&_mutex);
    return _lastWaitMs;
}

QString RequestPacer::currentPacingStatus() const
{
    QMutexLocker locker(&_mutex);
    return _statusMsg;
}

