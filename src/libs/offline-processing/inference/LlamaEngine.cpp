#include "LlamaEngine.h"

// Qt
#include <QMutexLocker>
#include <QMetaObject>

// C++ standard
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>

constexpr int kDefaultGenContextSize = 8192;

// ─────────────────────────────────────────────────────────────
// Determine optimal CPU worker thread count.
// Clamped to physical core count (2-8) to avoid SMT cache thrashing.
// ─────────────────────────────────────────────────────────────
static int resolveThreadCount()
{
    bool ok = false;
    int envThreads = qEnvironmentVariableIntValue("CARBON_ENGINE_THREADS", &ok);
    if (ok && envThreads > 0) {
        return envThreads;
    }

    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    if (hw > 8)  hw = 8;
    if (hw < 2)  hw = 2;
    return static_cast<int>(hw);
}

// ─────────────────────────────────────────────────────────────
LlamaEngine::LlamaEngine(const QString& genModelPath,
                         const QString& embdModelPath,
                         SystemMonitor& monitor,
                         QObject* parent)
    : QObject(parent)
    , _genPath(genModelPath.toStdString())
    , _embdPath(embdModelPath.toStdString())
    , _nThreads(resolveThreadCount())
    , _monitor(monitor)
{
    bool ok = false;
    int envTimeout = qEnvironmentVariableIntValue("CARBON_ENGINE_IDLE_TIMEOUT_SEC", &ok);
    if (ok && envTimeout >= 0) {
        _idleTimeoutSec = envTimeout;
    }

    _idleTimer = new QTimer(this);
    _idleTimer->setSingleShot(true);
    connect(_idleTimer, &QTimer::timeout, this, &LlamaEngine::handleIdleTimeout);
}

LlamaEngine::~LlamaEngine()
{
    shutdown();
    llama_backend_free();
}

void LlamaEngine::shutdown()
{
    if (_idleTimer) {
        _idleTimer->stop();
    }
    unloadGenModel();
    unloadEmbdModel();
}

int LlamaEngine::resolveGenGpuLayers() const
{
    bool ok = false;
    int envVal = qEnvironmentVariableIntValue("CARBON_ENGINE_GEN_GPU_LAYERS", &ok);
    if (!ok) {
        envVal = qEnvironmentVariableIntValue("CARBON_ENGINE_GPU_LAYERS", &ok);
    }
    if (ok) {
        _log.info(QString("Generative GPU layers overridden: %1").arg(envVal));
        return envVal;
    }

    if (!_monitor.gpuPresent()) {
        _log.info("No dedicated GPU detected. Defaulting to CPU backend for generation (n_gpu_layers = 0).");
        return 0;
    }

    quint64 vramMb = _monitor.vramTotalMb();
    _log.info(QString("Dedicated GPU detected with %1 MB VRAM for budgeting").arg(vramMb));

    // Dynamic layer budgeting based on VRAM capacity:
    // A 7B Q4_K_M model requires ~4.7 GB for weights + ~1.5 GB for KV cache = ~6.2 GB total for 100% offload.
    if (vramMb >= 7500) {
        _log.info("Sufficient VRAM detected (>= 7.5GB). Offloading all layers (n_gpu_layers = 99).");
        return 99;
    } else if (vramMb >= 5500) {
        _log.info("Moderate VRAM detected (5.5GB - 7.5GB). Using hybrid partial offload (n_gpu_layers = 24).");
        return 24;
    } else if (vramMb >= 3500) {
        _log.info("Limited VRAM detected (3.5GB - 5.5GB). Using conservative partial offload (n_gpu_layers = 14).");
        return 14;
    } else {
        _log.info("Very low VRAM detected (< 3.5GB). Running on CPU to prevent out-of-memory crashes.");
        return 0;
    }
}

int LlamaEngine::resolveEmbdGpuLayers() const
{
    bool ok = false;
    int envVal = qEnvironmentVariableIntValue("CARBON_ENGINE_EMBD_GPU_LAYERS", &ok);
    if (ok) {
        _log.info(QString("Embedding GPU layers overridden: %1").arg(envVal));
        return envVal;
    }

    if (!_monitor.gpuPresent()) {
        return 0;
    }

    quint64 vramMb = _monitor.vramTotalMb();
    // Joint budgeting: if VRAM >= 8GB, there is ample room for both 7B generative model and 67MB embedding model
    if (vramMb >= 8000) {
        _log.info("VRAM >= 8GB: Joint offload enabled for embedding model on GPU.");
        return 99;
    }

    // For GPUs with < 8GB VRAM (4GB / 6GB), keep embedding on CPU to preserve 100% of VRAM for the 7B model
    _log.info("VRAM < 8GB: Embedding model assigned to CPU to preserve VRAM for generative model.");
    return 0;
}

int LlamaEngine::activeGenGpuLayers() const
{
    return _activeGenLayers.load();
}

int LlamaEngine::activeEmbdGpuLayers() const
{
    return _activeEmbdLayers.load();
}

QString LlamaEngine::offloadModeString() const
{
    if (!_monitor.gpuPresent()) {
        return QStringLiteral("CPU Only");
    }

    int genLayers = _activeGenLayers.load();
    int embdLayers = _activeEmbdLayers.load();

    if (genLayers < 0) {
        int target = resolveGenGpuLayers();
        if (target >= 99) return QStringLiteral("Ready (Full GPU)");
        if (target > 0) return QString("Ready (Hybrid %1L)").arg(target);
        return QStringLiteral("Ready (CPU)");
    }

    if (genLayers >= 99) {
        return (embdLayers >= 99) ? QStringLiteral("Full GPU (Dual)") : QStringLiteral("Full GPU (99L)");
    } else if (genLayers > 0) {
        return QString("Hybrid (%1L)").arg(genLayers);
    }
    return QStringLiteral("CPU Only (0L)");
}

void LlamaEngine::resetIdleTimer()
{
    if (!_idleTimer || _idleTimeoutSec <= 0) return;

    QMetaObject::invokeMethod(_idleTimer, [this]() {
        if (_idleTimer && _idleTimeoutSec > 0) {
            _idleTimer->start(_idleTimeoutSec * 1000);
        }
    }, Qt::QueuedConnection);
}

void LlamaEngine::scheduleIdleUnload(int timeoutSeconds)
{
    if (timeoutSeconds > 0) {
        _idleTimeoutSec = timeoutSeconds;
    }
    resetIdleTimer();
}

void LlamaEngine::handleIdleTimeout()
{
    if (_isInferencing.load()) {
        resetIdleTimer();
        return;
    }
    _log.info(QString("Idle timeout (%1s) expired: unloading generation model to reclaim RAM/VRAM").arg(_idleTimeoutSec));
    unloadGenModel();
}

bool LlamaEngine::isGenModelLoaded() const
{
    QMutexLocker lock(&_genMutex);
    return (_genModel != Q_NULLPTR);
}

bool LlamaEngine::isEmbdModelLoaded() const
{
    QMutexLocker lock(&_embdMutex);
    return (_embdModel != Q_NULLPTR);
}

bool LlamaEngine::isInferencing() const
{
    return _isInferencing.load();
}

// ─────────────────────────────────────────────────────────────
bool LlamaEngine::initBackend()
{
    llama_backend_init();
    _log.info(QString("LlamaEngine: backend initialised, using %1 threads (idle keep-alive: %2s)")
                  .arg(_nThreads).arg(_idleTimeoutSec));
    return true;
}

// ─────────────────────────────────────────────────────────────
// Embedding model
// ─────────────────────────────────────────────────────────────
bool LlamaEngine::loadEmbdModel()
{
    QMutexLocker lock(&_embdMutex);
    if (_embdModel) {
        return true;
    }

    _monitor.setEnginePhase(EnginePhase::LoadingModel);
    _log.info("Loading embedding model...");

    llama_model_params mp = llama_model_default_params();
    int embdLayers = resolveEmbdGpuLayers();
    mp.n_gpu_layers = embdLayers;

    _embdModel = llama_model_load_from_file(_embdPath.c_str(), mp);
    if (!_embdModel && embdLayers > 0) {
        _log.warn("Failed to load embedding model on GPU. Retrying with CPU fallback...");
        mp.n_gpu_layers = 0;
        embdLayers = 0;
        _embdModel = llama_model_load_from_file(_embdPath.c_str(), mp);
    }
    if (!_embdModel) {
        _log.error(QString("Failed to load embedding model: %1")
                       .arg(QString::fromStdString(_embdPath)));
        _monitor.setEnginePhase(EnginePhase::Idle);
        return false;
    }

    if (!initEmbdContext()) {
        llama_model_free(_embdModel);
        _embdModel = Q_NULLPTR;
        _monitor.setEnginePhase(EnginePhase::Idle);
        return false;
    }

    _activeEmbdLayers.store(embdLayers);
    _log.info(QString("Embedding model loaded (%1 GPU layers active)").arg(embdLayers));
    _monitor.setEnginePhase(EnginePhase::Idle);
    return true;
}

bool LlamaEngine::initEmbdContext()
{
    llama_context_params cp = llama_context_default_params();
    // BGE-small-en-v1.5 has max position embeddings of 512
    cp.n_ctx           = 512;
    cp.n_batch         = 512;
    cp.n_ubatch        = 512;
    cp.n_threads       = _nThreads;
    cp.n_threads_batch = _nThreads;
    cp.embeddings      = true;
    cp.pooling_type    = LLAMA_POOLING_TYPE_UNSPECIFIED;

    _embdCtx = llama_init_from_model(_embdModel, cp);
    if (!_embdCtx) {
        _log.error("Failed to create embedding context");
        return false;
    }
    return true;
}

void LlamaEngine::unloadEmbdModel()
{
    QMutexLocker lock(&_embdMutex);
    _activeEmbdLayers.store(0);
    if (_embdCtx) {
        llama_free(_embdCtx);
        _embdCtx = Q_NULLPTR;
    }
    if (_embdModel) {
        llama_model_free(_embdModel);
        _embdModel = Q_NULLPTR;
        _log.info("Embedding model unloaded");
    }
}

// ─────────────────────────────────────────────────────────────
// Generation model
// ─────────────────────────────────────────────────────────────
bool LlamaEngine::loadGenModel()
{
    QMutexLocker lock(&_genMutex);
    if (_genModel) {
        resetIdleTimer();
        return true;
    }

    _monitor.setEnginePhase(EnginePhase::LoadingModel);

    int targetGpuLayers = resolveGenGpuLayers();
    std::vector<int> attempts;
    attempts.push_back(targetGpuLayers);

    // If target GPU layers was high, prepare resilient tiered fallbacks in case of VRAM exhaustion
    if (targetGpuLayers > 20) {
        attempts.push_back(14); // Conservative partial offload
    }
    if (targetGpuLayers > 0) {
        attempts.push_back(0);  // Safe 100% CPU fallback
    }

    for (int layers : attempts) {
        _log.info(QString("Attempting to load generation model with %1 GPU layers...").arg(layers));

        llama_model_params mp = llama_model_default_params();
        mp.n_gpu_layers = layers;

        _genModel = llama_model_load_from_file(_genPath.c_str(), mp);
        if (!_genModel) {
            _log.warn(QString("Failed to allocate model weights with %1 GPU layers (VRAM may be constrained). Retrying fallback tier...").arg(layers));
            continue;
        }

        if (!initGenContext()) {
            _log.warn(QString("Failed to allocate context/KV cache with %1 GPU layers. Freeing model and retrying fallback tier...").arg(layers));
            llama_model_free(_genModel);
            _genModel = Q_NULLPTR;
            continue;
        }

        // Successfully loaded model and initialized context!
        _activeGenLayers.store(layers);
        _log.info(QString("Generation model successfully loaded (%1 GPU layers active)").arg(layers));
        _monitor.setEnginePhase(EnginePhase::Idle);
        resetIdleTimer();
        return true;
    }

    _activeGenLayers.store(-1);
    _log.error(QString("Failed to load generation model across all hardware tiers: %1")
                   .arg(QString::fromStdString(_genPath)));
    _monitor.setEnginePhase(EnginePhase::Idle);
    return false;
}

void LlamaEngine::unloadGenModel()
{
    QMutexLocker lock(&_genMutex);
    _activeGenLayers.store(-1);
    if (_genCtx) {
        llama_free(_genCtx);
        _genCtx = Q_NULLPTR;
    }
    if (_genModel) {
        llama_model_free(_genModel);
        _genModel = Q_NULLPTR;
        _log.info("Generation model unloaded");
    }
}

bool LlamaEngine::initGenContext()
{
    quint64 vramMb = _monitor.vramTotalMb();
    int ctxSize = (vramMb > 0 && vramMb < 6000) ? 4096 : kDefaultGenContextSize;

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx           = ctxSize;
    cp.n_threads       = _nThreads;
    cp.n_batch         = 2048;
    cp.n_ubatch        = 512;
    cp.n_threads_batch = _nThreads;

    // In constrained VRAM setups (< 8GB), use 8-bit quantized KV cache to halve VRAM footprint
    if (vramMb > 0 && vramMb < 8000) {
        cp.type_k = GGML_TYPE_Q8_0;
        cp.type_v = GGML_TYPE_Q8_0;
        _log.info("Using Q8_0 quantized KV cache to conserve VRAM");
    }

    _genCtx = llama_init_from_model(_genModel, cp);
    if (!_genCtx) {
        // Fallback: retry with 4096 context if 8192 context failed to allocate
        if (ctxSize > 4096) {
            _log.warn("Context creation failed at 8192 tokens. Retrying with 4096 context to reduce memory...");
            cp.n_ctx = 4096;
            _genCtx = llama_init_from_model(_genModel, cp);
        }
    }

    if (!_genCtx) {
        _log.error("Failed to create generation context");
        return false;
    }
    return true;
}


// ─────────────────────────────────────────────────────────────
// Inference
// ─────────────────────────────────────────────────────────────
std::vector<float> LlamaEngine::generateEmbedding(const std::string& text)
{
    auto batch = generateEmbeddingsBatch({text});
    if (batch.empty()) {
        return {};
    }
    return std::move(batch[0]);
}

std::vector<std::vector<float>> LlamaEngine::generateEmbeddingsBatch(const std::vector<std::string>& texts)
{
    if (texts.empty()) {
        return {};
    }

    QMutexLocker locker(&_embdMutex);

    if (!_embdModel || !_embdCtx) {
        _log.error("generateEmbeddingsBatch: embedding model not loaded");
        return std::vector<std::vector<float>>(texts.size());
    }

    _monitor.setEnginePhase(EnginePhase::Embedding);

    const llama_vocab* vocab = llama_model_get_vocab(_embdModel);
    int nEmbd = llama_model_n_embd(_embdModel);
    std::vector<std::vector<float>> allResults;
    allResults.reserve(texts.size());

    for (const auto& text : texts) {
        std::vector<llama_token> tokens(text.size() + 8);
        int nTokens = llama_tokenize(vocab, text.c_str(), static_cast<int32_t>(text.length()),
                                     tokens.data(), static_cast<int32_t>(tokens.size()), true, false);
        if (nTokens < 0) {
            tokens.resize(-nTokens);
            nTokens = llama_tokenize(vocab, text.c_str(), static_cast<int32_t>(text.length()),
                                     tokens.data(), static_cast<int32_t>(tokens.size()), true, false);
        }

        if (nTokens <= 0) {
            allResults.push_back({});
            continue;
        }
        tokens.resize(nTokens);

        // Clamp to max 512 tokens for BERT embedding context
        if (tokens.size() > 512) {
            tokens.resize(512);
        }

        llama_memory_clear(llama_get_memory(_embdCtx), true);

        llama_batch batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));
        if (llama_decode(_embdCtx, batch) != 0) {
            allResults.push_back({});
            continue;
        }

        const float* embPtr = llama_get_embeddings_seq(_embdCtx, 0);
        if (!embPtr) {
            embPtr = llama_get_embeddings_ith(_embdCtx, -1);
        }
        if (!embPtr) {
            allResults.push_back({});
            continue;
        }

        std::vector<float> vec(embPtr, embPtr + nEmbd);

        // L2 unit normalization for cosine similarity in FAISS
        float sumSq = 0.0f;
        for (float v : vec) {
            sumSq += v * v;
        }
        if (sumSq > 1e-12f) {
            float invNorm = 1.0f / std::sqrt(sumSq);
            for (float& v : vec) {
                v *= invNorm;
            }
        }
        allResults.push_back(std::move(vec));
    }

    _monitor.setEnginePhase(EnginePhase::Idle);
    return allResults;
}

void LlamaEngine::streamCompletion(const std::string& prompt,
                                   std::function<bool(const std::string&)> tokenCallback,
                                   const GenerationOptions& options)
{
    {
        QMutexLocker locker(&_genMutex);
        if (!_genModel || !_genCtx) {
            locker.unlock();
            _log.info("streamCompletion: generation model not loaded, attempting on-demand load...");
            if (!loadGenModel()) {
                _log.error("streamCompletion: generation model could not be loaded");
                return;
            }
        }
    }

    QMutexLocker locker(&_genMutex);

    if (!_genModel || !_genCtx) {
        _log.error("streamCompletion: generation model not loaded");
        return;
    }

    _isInferencing.store(true);
    resetIdleTimer();
    _monitor.setEnginePhase(EnginePhase::Inferencing);

    struct InferGuard {
        std::atomic<bool>& inferencingFlag;
        SystemMonitor& monitor;
        LlamaEngine& engine;
        ~InferGuard() {
            inferencingFlag.store(false);
            monitor.setEnginePhase(EnginePhase::Idle);
            engine.resetIdleTimer();
        }
    } inferGuard{_isInferencing, _monitor, *this};

    const llama_vocab* vocab = llama_model_get_vocab(_genModel);
    std::vector<llama_token> tokens(prompt.size() + 8);
    int nTokens = llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.length()),
                                 tokens.data(), static_cast<int32_t>(tokens.size()), true, true);
    if (nTokens < 0) {
        tokens.resize(-nTokens);
        nTokens = llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.length()),
                                 tokens.data(), static_cast<int32_t>(tokens.size()), true, true);
        if (nTokens < 0) {
            return;
        }
    }
    tokens.resize(nTokens);

    if (tokens.empty()) {
        return;
    }

    uint32_t genCtxSize = llama_n_ctx(_genCtx);
    int maxTokensToGen = options.maxTokens > 0 ? options.maxTokens : 2048;

    // Ensure prompt + max generated tokens fits comfortably in the context window.
    // Preserve head (system instructions) and tail (query & assistant turn), truncating middle context.
    size_t maxAllowedPrompt = (genCtxSize > static_cast<uint32_t>(maxTokensToGen))
                                  ? (genCtxSize - maxTokensToGen)
                                  : 512;
    if (tokens.size() > maxAllowedPrompt) {
        _log.warn(QString("Prompt length (%1 tokens) exceeds context limit (%2 - %3). Truncating middle context to preserve instructions.")
                      .arg(tokens.size()).arg(genCtxSize).arg(maxTokensToGen));
        size_t keepTail = std::min(size_t(512), maxAllowedPrompt / 2);
        size_t keepHead = maxAllowedPrompt - keepTail;
        std::vector<llama_token> truncated;
        truncated.reserve(maxAllowedPrompt);
        truncated.insert(truncated.end(), tokens.begin(), tokens.begin() + keepHead);
        truncated.insert(truncated.end(), tokens.end() - keepTail, tokens.end());
        tokens = std::move(truncated);
    }

    llama_memory_clear(llama_get_memory(_genCtx), true);

    // Build per-request sampler chain based on GenerationOptions
    llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    llama_sampler* sampler = llama_sampler_chain_init(sp);

    int32_t n_vocab = llama_vocab_n_tokens(vocab);
    if (options.repeatPenalty > 1.0f) {
        llama_sampler_chain_add(sampler, llama_sampler_init_penalties(n_vocab, 64, options.repeatPenalty, 0.10f, 0.10f));
    }
    if (options.temperature <= 0.01f) {
        // Deterministic greedy argmax sampling for factual extraction
        llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.0f));
    } else {
        llama_sampler_chain_add(sampler, llama_sampler_init_top_k(options.topK));
        llama_sampler_chain_add(sampler, llama_sampler_init_top_p(options.topP, 1));
        llama_sampler_chain_add(sampler, llama_sampler_init_temp(options.temperature));
        llama_sampler_chain_add(sampler, llama_sampler_init_dist(0));
    }

    struct SamplerGuard {
        llama_sampler* s;
        ~SamplerGuard() { if (s) llama_sampler_free(s); }
    } samplerGuard{sampler};

    uint32_t n_batch = llama_n_batch(_genCtx);
    llama_batch batch = llama_batch_init(n_batch, 0, 1);
    struct BatchGuard {
        llama_batch& b;
        ~BatchGuard() { llama_batch_free(b); }
    } batchGuard{batch};

    // Ingest prompt in batches
    for (size_t i = 0; i < tokens.size(); i += n_batch) {
        int n_eval = std::min(static_cast<int>(n_batch),
                              static_cast<int>(tokens.size() - i));
        batch.n_tokens = 0;
        for (int j = 0; j < n_eval; j++) {
            bool isLast = (i + j == tokens.size() - 1);
            batch.token[batch.n_tokens]       = tokens[i + j];
            batch.pos[batch.n_tokens]         = static_cast<llama_pos>(i + j);
            batch.n_seq_id[batch.n_tokens]    = 1;
            batch.seq_id[batch.n_tokens][0]   = 0;
            batch.logits[batch.n_tokens]      = isLast;
            batch.n_tokens++;
        }
        if (llama_decode(_genCtx, batch) != 0) {
            _log.error("Failed to decode prompt batch");
            return;
        }
    }

    // Autoregressive generation loop
    auto tStart = std::chrono::steady_clock::now();
    int tokensGenerated = 0;
    std::vector<char> pieceBuf(256);

    for (int i = 0; i < maxTokensToGen; ++i) {
        llama_token nextToken = llama_sampler_sample(sampler, _genCtx, -1);
        llama_sampler_accept(sampler, nextToken);

        if (llama_vocab_is_eog(vocab, nextToken)) {
            break;
        }

        int n = llama_token_to_piece(vocab, nextToken, pieceBuf.data(), static_cast<int32_t>(pieceBuf.size()), 0, true);
        if (n < 0) {
            pieceBuf.resize(-n);
            n = llama_token_to_piece(vocab, nextToken, pieceBuf.data(), static_cast<int32_t>(pieceBuf.size()), 0, true);
        }

        if (n > 0) {
            std::string piece(pieceBuf.data(), n);
            if (piece.find("<|im_end|>") != std::string::npos ||
                piece.find("<|endoftext|>") != std::string::npos) {
                break;
            }
            if (!tokenCallback(piece)) {
                break;
            }
        }

        ++tokensGenerated;

        // Periodic live token rate update so UI poller captures active throughput
        if (tokensGenerated % 6 == 0) {
            auto now = std::chrono::steady_clock::now();
            double secs = std::chrono::duration<double>(now - tStart).count();
            if (secs > 0.05) {
                _monitor.setTokensPerSec(static_cast<double>(tokensGenerated) / secs);
            }
        }

        // Check if context limit reached
        if (tokens.size() + i + 1 >= genCtxSize) {
            _log.warn("Context size limit reached during generation");
            break;
        }

        batch.n_tokens = 0;
        batch.token[batch.n_tokens]     = nextToken;
        batch.pos[batch.n_tokens]       = static_cast<llama_pos>(tokens.size() + i);
        batch.n_seq_id[batch.n_tokens]  = 1;
        batch.seq_id[batch.n_tokens][0] = 0;
        batch.logits[batch.n_tokens]    = true;
        batch.n_tokens++;

        if (llama_decode(_genCtx, batch) != 0) {
            break;
        }
    }

    // Measure and publish tokens/sec
    auto elapsed = std::chrono::steady_clock::now() - tStart;
    double secs  = std::chrono::duration<double>(elapsed).count();
    if (secs > 0.0 && tokensGenerated > 0) {
        _monitor.setTokensPerSec(static_cast<double>(tokensGenerated) / secs);
    }
}
