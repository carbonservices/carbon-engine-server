#pragma once
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <QObject>
#include <QMutex>
#include <QTimer>
#include <QtGlobal>
#include "llama.h"
#include "logging/Logger.h"
#include "system/SystemMonitor.h"

struct GenerationOptions {
    float   temperature   = 0.7f;
    float   topP          = 0.9f;
    int32_t topK          = 40;
    float   repeatPenalty = 1.15f;
    int     maxTokens     = 2048;
};

class LlamaEngine : public QObject {
    Q_OBJECT

public:
    explicit LlamaEngine(const QString& genModelPath,
                         const QString& embdModelPath,
                         SystemMonitor& monitor,
                         QObject* parent = Q_NULLPTR);
    ~LlamaEngine() override;

    bool initBackend();

    bool loadEmbdModel();
    void unloadEmbdModel();

    bool loadGenModel();
    void unloadGenModel();

    void resetIdleTimer();
    void scheduleIdleUnload(int timeoutSeconds = 300);

    void shutdown();

    bool isGenModelLoaded() const;
    bool isEmbdModelLoaded() const;
    bool isInferencing() const;

    int activeGenGpuLayers() const;
    int activeEmbdGpuLayers() const;
    QString offloadModeString() const;

    std::vector<float> generateEmbedding(const std::string& text);
    std::vector<std::vector<float>> generateEmbeddingsBatch(const std::vector<std::string>& texts);

    void streamCompletion(const std::string& prompt,
                          std::function<bool(const std::string&)> tokenCallback,
                          const GenerationOptions& options = GenerationOptions{});

private slots:
    void handleIdleTimeout();

private:
    bool initGenContext();
    bool initEmbdContext();
    int  resolveGenGpuLayers() const;
    int  resolveEmbdGpuLayers() const;

    std::string _genPath;
    std::string _embdPath;
    int         _nThreads = 4;
    int         _idleTimeoutSec = 300;

    llama_model*   _genModel  = Q_NULLPTR;
    llama_context* _genCtx    = Q_NULLPTR;
    llama_model*   _embdModel = Q_NULLPTR;
    llama_context* _embdCtx   = Q_NULLPTR;

    mutable QMutex _genMutex;
    mutable QMutex _embdMutex;

    QTimer*           _idleTimer = Q_NULLPTR;
    std::atomic<bool> _isInferencing{false};
    std::atomic<int>  _activeGenLayers{-1};
    std::atomic<int>  _activeEmbdLayers{0};

    SystemMonitor& _monitor;
    Logger _log{"LlamaEngine"};
};

