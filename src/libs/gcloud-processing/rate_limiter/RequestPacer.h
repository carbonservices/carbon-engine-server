#pragma once

#include <QMutex>
#include <QElapsedTimer>
#include <QString>
#include <chrono>

class RequestPacer {
public:
    static RequestPacer& instance();

    void acquireGenerationSlot();
    void acquireEmbeddingSlot();

    void reportRateLimitError();
    void reportSuccess();

    qint64 lastPacingWaitMs() const;
    QString currentPacingStatus() const;

private:
    RequestPacer();
    void acquireSlot(std::chrono::steady_clock::time_point& nextTime, qint64 minIntervalMs, const QString& slotLabel);

    mutable QMutex _mutex;
    std::chrono::steady_clock::time_point _nextGenTime;
    std::chrono::steady_clock::time_point _nextEmbdTime;

    qint64 _minGenIntervalMs{4000};
    qint64 _minEmbdIntervalMs{650};
    qint64 _lastWaitMs{0};
    QString _statusMsg{"Normal"};
};

