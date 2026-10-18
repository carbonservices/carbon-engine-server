#include "ReportExtractionService.h"
#include "constants/CommonConstants.h"
#include "config/GCloudConfig.h"
#include "rate_limiter/RequestPacer.h"
#include "report/JsonRepairEngine.h"

// Qt
#include <QFutureSynchronizer>
#include <QtConcurrent>

// Templating
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace {

QString getLoopFieldHint(const QString& key) {
    return BRSRSchemaCatalog::loopFieldHint(key);
}

nlohmann::json parseOrRecoverJson(const std::string& raw) {
    return JsonRepairEngine::parseOrRecoverJson(raw);
}

} // namespace

ReportExtractionService::ReportExtractionService(Database& db, LlamaEngine& llama, GeminiClient& gemini,
                                                 VectorIndexRegistry& indexRegistry,
                                                 const BRSRSchemaCatalog& catalog,
                                                 SystemMonitor& monitor)
    : _db(db), _llama(llama), _gemini(gemini), _indexRegistry(indexRegistry), _catalog(catalog), _monitor(monitor), _factStore(&db)
{
}

ReportExtractionService::GroupContext ReportExtractionService::retrieveContextForGroup(
    int jobId, const FieldGroup& group)
{
    if (_cancelRequested.load()) {
        return {group.id, QString(), QVector<int64_t>(), 0};
    }

    QElapsedTimer retTimer;
    retTimer.start();

    std::vector<int64_t> allIds;
    std::unordered_set<int64_t> seenVectorIds;

    // 1. Vector Similarity Search first (FAISS HNSW k=6) - primary semantic relevance
    std::vector<float> queryEmbedding;
    bool isCloudEmb = GCloudConfig::instance().isCloudEnabled();
    if (isCloudEmb) {
        QString embError;
        queryEmbedding = _geminiEmbedding.generateEmbedding(group.ragQuery.toStdString(), embError);
        int retryCount = 0;
        while (queryEmbedding.empty() && retryCount < 3 && !_cancelRequested.load()) {
            _log.warn(QString("Cloud embedding query failed (attempt %1): %2. Retrying in 1s...").arg(retryCount + 1).arg(embError));
            QThread::msleep(1000);
            queryEmbedding = _geminiEmbedding.generateEmbedding(group.ragQuery.toStdString(), embError);
            retryCount++;
        }
    } else {
        queryEmbedding = _llama.generateEmbedding(group.ragQuery.toStdString());
    }

    if (!queryEmbedding.empty()) {
        auto index = _indexRegistry.getOrCreate(jobId);
        std::vector<int64_t> vectorIds = index ? index->search(queryEmbedding, 6) : std::vector<int64_t>{};
        for (int64_t vectorId : vectorIds) {
            if (vectorId >= 0 && seenVectorIds.insert(vectorId).second) {
                allIds.push_back(vectorId);
            }
        }
    }

    if (_cancelRequested.load()) {
        return {group.id, QString(), QVector<int64_t>(), 0};
    }

    // 2. Locality-anchored chunks from FactStore (topic anchors + immediate +/- 1 neighbors)
    QVector<int64_t> anchorIds = _factStore.getAnchorChunksForTopic(jobId, group.section);
    for (int64_t anchorId : anchorIds) {
        if (anchorId > 0) {
            for (int64_t delta = -1; delta <= 1; ++delta) {
                int64_t neighborId = anchorId + delta;
                if (neighborId > 0 && seenVectorIds.insert(neighborId).second) {
                    allIds.push_back(neighborId);
                }
            }
        }
    }

    if (_cancelRequested.load()) {
        return {group.id, QString(), QVector<int64_t>(), 0};
    }

    // 3. Batched single-query SQLite lookup with strict character budget (~2,200 tokens)
    constexpr int kMaxContextChars = 9000;
    auto chunkMap = _db.getChunksByIds(allIds);
    QString assembledContext;
    QVector<int64_t> matchedIds;

    for (int64_t id : allIds) {
        auto it = chunkMap.find(id);
        if (it != chunkMap.end() && !it.value().isEmpty()) {
            if (assembledContext.length() + it.value().length() > kMaxContextChars) {
                if (!matchedIds.isEmpty()) {
                    break;
                }
            }
            assembledContext += it.value() + "\n\n";
            matchedIds.push_back(id);
        }
    }

    qint64 elapsedMs = retTimer.elapsed();
    return {group.id, assembledContext, matchedIds, elapsedMs};
}

QString ReportExtractionService::buildInstructions(int jobId, const FieldGroup& group) const
{
    QString fieldList;
    for (const QString& key : group.dataFieldKeys) {
        fieldList += "- " + key + ": a precise numeric or short factual value\n";
    }
    for (const QString& key : group.narrativeFieldKeys) {
        fieldList += "- " + key + ": a short narrative paragraph grounded only in the context below\n";
    }
    for (const QString& key : group.loopFieldKeys) {
        fieldList += "- " + key + ": " + getLoopFieldHint(key) + "\n";
    }

    QString knownFacts = _factStore.formatKnownFactsForPrompt(jobId);

    QString instructions =
        "You are a strict data extraction AI for Business Responsibility and Sustainability "
        "Reporting (BRSR), currently extracting only the \"" + group.label + "\" section.\n"
        + knownFacts +
        "Extract exactly these fields from the context into a flat JSON object:\n" + fieldList +
        "\nRules:\n"
        "1. Output ONLY a valid JSON object starting with { and ending with }.\n"
        "2. If a field cannot be found in the context, set its value to null (or [] for array fields).\n"
        "3. Do NOT include markdown code fences, notes, or explanations.\n"
        "4. Narrative fields must be grounded only in the provided context, not invented.";

    return instructions;
}

std::string ReportExtractionService::buildGemmaPrompt(int jobId, const FieldGroup& group, const QString& context) const
{
    QString instructions = buildInstructions(jobId, group);
    std::string prompt =
        "<start_of_turn>user\n" + instructions.toStdString() +
        "\n\nContext:\n" + context.toStdString() +
        "\n\nExtract JSON.<end_of_turn>\n"
        "<start_of_turn>model\n{";
    return prompt;
}

void ReportExtractionService::generateAndPersistGroup(int jobId, const FieldGroup& group,
                                                     const GroupContext& ctx,
                                                     qint64& outPrefillMs, qint64& outDecodeMs,
                                                     int& outTokensGenerated,
                                                     const QString& explicitApiKey)
{
    QElapsedTimer genTimer;
    genTimer.start();

    std::string generatedJson;
    int tokenCount = 0;
    qint64 prefillTime = 0;
    bool isCloud = GCloudConfig::instance().isCloudEnabled();

    if (isCloud) {
        {
            QMutexLocker locker(&_progressMutex);
            _progress.engineMode = EngineMode::GCloud;
            _progress.activeModel = GCloudConfig::instance().modelName();
            _progress.pacingStatus = RequestPacer::instance().currentPacingStatus();
        }

        QString instructions = buildInstructions(jobId, group);
        _monitor.setEnginePhase(EnginePhase::Inferencing);
        auto cloudResult = _gemini.generateExtraction(instructions, ctx.assembledContext, explicitApiKey);

        if (_cancelRequested.load()) {
            _monitor.setEnginePhase(EnginePhase::Idle);
            return;
        }

        while (!cloudResult.success) {
            if (_cancelRequested.load()) {
                _monitor.setEnginePhase(EnginePhase::Idle);
                return;
            }

            _log.warn(QString("[RateLimit] Cloud extraction failed for section '%1' (model %2): %3")
                .arg(group.label).arg(GCloudConfig::instance().modelName()).arg(cloudResult.error));

            {
                QMutexLocker locker(&_progressMutex);
                _progress.isRateLimited = true;
                _progress.rateLimitError = cloudResult.error;
                _progress.rateLimitedModel = GCloudConfig::instance().modelName();
                _progress.rateLimitedSection = group.label;
                _progress.rateLimitType = "INFERENCE";
            }
            _db.setExtractionStatus(jobId, "RATE_LIMITED");
            _isRateLimited.store(true);

            // Wait for user resolution
            {
                QMutexLocker rLock(&_rateLimitMutex);
                _rateLimitResolution = RateLimitResolution::None;
                while (_isRateLimited.load() && !_cancelRequested.load() && _rateLimitResolution == RateLimitResolution::None) {
                    _rateLimitCondition.wait(&_rateLimitMutex, 1000);
                }
            }

            {
                QMutexLocker locker(&_progressMutex);
                _progress.isRateLimited = false;
                _progress.rateLimitError.clear();
            }
            _isRateLimited.store(false);

            if (_cancelRequested.load() || _rateLimitResolution == RateLimitResolution::Cancel) {
                _monitor.setEnginePhase(EnginePhase::Idle);
                _db.setExtractionStatus(jobId, ExtractionStatus::Failed);
                return;
            }

            if (_rateLimitResolution == RateLimitResolution::SwitchToLocal) {
                _log.info(QString("[RateLimit] User switched to local inference for job %1").arg(jobId));
                GCloudConfig::instance().setCloudEnabled(false);
                _db.setExtractionStatus(jobId, ExtractionStatus::Extracting);
                isCloud = false;
                break;
            }

            if (_rateLimitResolution == RateLimitResolution::SwitchCloudModel) {
                if (!_pendingNewModel.isEmpty()) {
                    _log.info(QString("[RateLimit] User switched cloud model to '%1'").arg(_pendingNewModel));
                    GCloudConfig::instance().setModelName(_pendingNewModel);
                }
                _db.setExtractionStatus(jobId, ExtractionStatus::Extracting);
                cloudResult = _gemini.generateExtraction(instructions, ctx.assembledContext, explicitApiKey);
                continue;
            }

            if (_rateLimitResolution == RateLimitResolution::Retry) {
                _log.info(QString("[RateLimit] Retrying section with model '%1'").arg(GCloudConfig::instance().modelName()));
                _db.setExtractionStatus(jobId, ExtractionStatus::Extracting);
                cloudResult = _gemini.generateExtraction(instructions, ctx.assembledContext, explicitApiKey);
                continue;
            }
        }

        if (cloudResult.success) {
            generatedJson = cloudResult.jsonResponse.toStdString();
            outPrefillMs = 0;
            outDecodeMs = cloudResult.latencyMs;
            outTokensGenerated = cloudResult.candidateTokens;
            tokenCount = cloudResult.candidateTokens;

            if (cloudResult.latencyMs > 0 && cloudResult.candidateTokens > 0) {
                double tps = static_cast<double>(cloudResult.candidateTokens) / (static_cast<double>(cloudResult.latencyMs) / 1000.0);
                _monitor.setTokensPerSec(tps);
            }

            {
                QMutexLocker locker(&_progressMutex);
                _progress.lastLatencyMs = cloudResult.latencyMs;
            }
            _log.info(QString("[Cloud] Section '%1' extracted via %2 in %3ms (%4 tokens)")
                .arg(group.id).arg(GCloudConfig::instance().modelName()).arg(cloudResult.latencyMs).arg(cloudResult.candidateTokens));
        }
        _monitor.setEnginePhase(EnginePhase::Idle);
    }

    if (!isCloud) {
        if (!_llama.isGenModelLoaded()) {
            _log.info("Local generation model not loaded. Loading Gemma 2 2B for fallback extraction...");
            if (!_llama.loadGenModel()) {
                _log.error("Failed to load local Gemma 2 2B model for fallback extraction");
                return;
            }
        }

        {
            QMutexLocker locker(&_progressMutex);
            _progress.engineMode = EngineMode::Offline;
            _progress.activeModel = Models::LocalGemmaName;
            _progress.pacingStatus = "Local GPU/CPU";
        }

        std::string prompt = buildGemmaPrompt(jobId, group, ctx.assembledContext);

        int braceCount = 1; // Pre-seeded with opening brace '{'
        bool jsonStarted = true;
        bool inString = false;
        bool escapeNext = false;
        bool firstTokenReceived = false;

        generatedJson = "{";

        GenerationOptions options;
        options.temperature   = 0.0f;
        options.maxTokens     = group.loopFieldKeys.isEmpty() ? 768 : 1536;
        options.repeatPenalty = 1.05f;

        _llama.streamCompletion(prompt, [&](const std::string& token) -> bool {
            if (_cancelRequested.load()) {
                return false;
            }
            if (!firstTokenReceived) {
                firstTokenReceived = true;
                prefillTime = genTimer.elapsed();
            }
            tokenCount++;
            generatedJson += token;

            for (size_t i = 0; i < token.size(); ++i) {
                char c = token[i];
                if (inString) {
                    if (escapeNext) {
                        escapeNext = false;
                    } else if (c == '\\') {
                        escapeNext = true;
                    } else if (c == '"') {
                        inString = false;
                    }
                } else {
                    if (c == '"') {
                        inString = true;
                    } else if (c == '{') {
                        braceCount++;
                        jsonStarted = true;
                    } else if (c == '}') {
                        braceCount--;
                        if (jsonStarted && braceCount == 0) {
                            return false;
                        }
                    }
                }
            }
            return true;
        });

        if (_cancelRequested.load()) {
            return;
        }

        qint64 totalGenMs = genTimer.elapsed();
        outPrefillMs = prefillTime;
        outDecodeMs = totalGenMs - prefillTime;
        outTokensGenerated = tokenCount;
    }

    qint64 totalGenMs = genTimer.elapsed();

    // Parse JSON & memoize facts into FactStore using high-resilience parser and field salvaging
    QStringList sourceIdStrings;
    for (int64_t id : ctx.sourceChunkIds) {
        sourceIdStrings.append(QString::number(id));
    }
    QString sourceChunksStr = sourceIdStrings.join(",");

    try {
        nlohmann::json extracted = parseOrRecoverJson(generatedJson);
        if (extracted.empty() && tokenCount > 0) {
            throw std::runtime_error("No valid JSON fields could be parsed or recovered from output");
        }

        _db.upsertReportSection(jobId, group.id, QString::fromStdString(extracted.dump()), ReportSectionStatus::Draft, sourceChunksStr);

        int nonNullFacts = 0;
        int64_t primaryChunkId = ctx.sourceChunkIds.isEmpty() ? -1 : ctx.sourceChunkIds.first();

        for (auto it = extracted.begin(); it != extracted.end(); ++it) {
            if (!it.value().is_null()) {
                std::string valStr;
                if (it.value().is_string()) {
                    valStr = it.value().get<std::string>();
                } else if (it.value().is_number()) {
                    valStr = it.value().dump();
                }
                if (!valStr.empty() && valStr != "null" && valStr != "N/A") {
                    _factStore.put(jobId, QString::fromStdString(it.key()), QString::fromStdString(valStr), primaryChunkId);
                    nonNullFacts++;
                }
            }
        }

        if (nonNullFacts >= 2 && primaryChunkId > 0) {
            _factStore.recordTopicAnchor(jobId, group.section, primaryChunkId, 0);
        }

        QStringList missingDataKeys;
        for (const QString& key : group.dataFieldKeys) {
            std::string keyStd = key.toStdString();
            if (!extracted.contains(keyStd) || extracted[keyStd].is_null()) {
                missingDataKeys.append(key);
            } else if (extracted[keyStd].is_string()) {
                std::string s = extracted[keyStd].get<std::string>();
                if (s.empty() || s == "null" || s == "N/A") {
                    missingDataKeys.append(key);
                }
            }
        }

        if (!missingDataKeys.isEmpty()) {
            QMutexLocker locker(&_progressMutex);
            _progress.missingSections.append(MissingSection{group.id, group.label, missingDataKeys});
            _log.info(QString("[Notice] Section '%1': %2 metrics could not be located in source PDFs: %3")
                .arg(group.label).arg(missingDataKeys.size()).arg(missingDataKeys.join(", ")));
        }

        double tps = outDecodeMs > 0 ? (tokenCount / (outDecodeMs / 1000.0)) : 0.0;
        _log.info(QString("[Perf] Section '%1' | Mode: %2 | Retrieval: %3ms (%4 chunks) | Gen: %5ms (%6 tok @ %7 t/s) | Facts: %8")
            .arg(group.id)
            .arg(isCloud ? "Cloud" : "Gemma2")
            .arg(ctx.retrievalMs)
            .arg(ctx.sourceChunkIds.size())
            .arg(totalGenMs)
            .arg(tokenCount)
            .arg(tps, 0, 'f', 1)
            .arg(nonNullFacts));

    } catch (const std::exception& e) {
        _db.upsertReportSection(jobId, group.id, "{}", ReportSectionStatus::Failed, sourceChunksStr);
        {
            QMutexLocker locker(&_progressMutex);
            _progress.missingSections.append(MissingSection{group.id, group.label, group.dataFieldKeys});
        }
        _log.error(QString("[Perf] Section '%1' JSON parse failed: %2").arg(group.id).arg(e.what()));
    }
}

void ReportExtractionService::cancelJob(int jobId)
{
    if (_activeJobId.load() == jobId) {
        _log.warn(QString("Cancellation requested for extracting job %1").arg(jobId));
        _cancelRequested.store(true);
        QMutexLocker rLock(&_rateLimitMutex);
        _rateLimitResolution = RateLimitResolution::Cancel;
        _rateLimitCondition.wakeAll();
    }
}

void ReportExtractionService::resolveRateLimit(RateLimitResolution resolution, const QString& newModel)
{
    QMutexLocker rLock(&_rateLimitMutex);
    _pendingNewModel = newModel;
    _rateLimitResolution = resolution;
    _isRateLimited.store(false);
    _rateLimitCondition.wakeAll();
}

void ReportExtractionService::extractJob(int jobId, const QString& explicitApiKey)
{
    if (_isExtracting.exchange(true)) {
        _log.warn(QString("Cannot start extraction for job %1: job %2 is already extracting")
                      .arg(jobId).arg(_activeJobId.load()));
        return;
    }

    QString jobApiKey = !explicitApiKey.isEmpty() ? explicitApiKey.trimmed() : _db.getJobApiKey(jobId);
    _activeJobId.store(jobId);
    _cancelRequested.store(false);

    struct ExtractionGuard {
        std::atomic<bool>& isExtracting;
        std::atomic<int>& activeJobId;
        std::atomic<bool>& cancelRequested;
        ~ExtractionGuard() {
            activeJobId.store(-1);
            cancelRequested.store(false);
            isExtracting.store(false);
        }
    } guard{_isExtracting, _activeJobId, _cancelRequested};

    try {
        QElapsedTimer totalJobTimer;
    totalJobTimer.start();

    _db.setExtractionStatus(jobId, ExtractionStatus::Extracting);
    _log.info(QString("Starting high-precision per-section extraction for job %1").arg(jobId));

    {
        QMutexLocker locker(&_progressMutex);
        _progress.currentStep = 0;
        _progress.totalSteps = _catalog.groups().size();
        _progress.currentGroupId = "";
        _progress.currentGroupLabel = "Retrieving vector contexts...";
        _progress.missingSections.clear();
    }

    if (_cancelRequested.load()) {
        _db.setExtractionStatus(jobId, ExtractionStatus::Cancelled);
        _log.warn(QString("Job %1 extraction cancelled before start").arg(jobId));
        return;
    }

    // 1. Initialize and load any persisted facts for this job
    _factStore.loadForJob(jobId);

    // 2. Parallel retrieval phase: one embedding + vector/locality search per group, run concurrently.
    QElapsedTimer swapTimer;
    swapTimer.start();
    qint64 embdModelLoadMs = 0;
    bool isCloudInit = GCloudConfig::instance().isCloudEnabled();
    if (!isCloudInit) {
        if (!_llama.loadEmbdModel()) {
            _db.setExtractionStatus(jobId, ExtractionStatus::Failed);
            _log.error(QString("Job %1: failed to load embedding model").arg(jobId));
            return;
        }
        embdModelLoadMs = swapTimer.elapsed();
    }

    QFutureSynchronizer<GroupContext> retrievalSync;
    for (const FieldGroup& group : _catalog.groups()) {
        retrievalSync.addFuture(QtConcurrent::run([this, jobId, group]() {
            return retrieveContextForGroup(jobId, group);
        }));
    }
    retrievalSync.waitForFinished();

    QHash<QString, GroupContext> contextByGroupId;
    qint64 totalRetrievalMs = 0;
    for (const QFuture<GroupContext>& future : retrievalSync.futures()) {
        GroupContext result = future.result();
        totalRetrievalMs += result.retrievalMs;
        contextByGroupId[result.groupId] = result;
    }

    if (_cancelRequested.load()) {
        _db.setExtractionStatus(jobId, ExtractionStatus::Cancelled);
        _log.warn(QString("Job %1 extraction cancelled after retrieval").arg(jobId));
        return;
    }

    bool isCloud = GCloudConfig::instance().isCloudEnabled();
    {
        QMutexLocker locker(&_progressMutex);
        _progress.engineMode = isCloud ? EngineMode::GCloud : EngineMode::Offline;
        _progress.activeModel = isCloud ? GCloudConfig::instance().modelName() : Models::LocalGemmaName;
    }

    // 3. Serial generation phase: load local Gemma model only if in offline mode
    qint64 genModelLoadMs = 0;
    if (!isCloud) {
        swapTimer.restart();
        if (!_llama.loadGenModel()) {
            _db.setExtractionStatus(jobId, ExtractionStatus::Failed);
            _log.error(QString("Job %1: failed to load Gemma generation model").arg(jobId));
            return;
        }
        genModelLoadMs = swapTimer.elapsed();
    }

    qint64 totalPrefillMs = 0;
    qint64 totalDecodeMs = 0;
    int totalTokens = 0;
    int extractedCount = 0;
    int skippedCount = 0;

    for (const FieldGroup& group : _catalog.groups()) {
        if (_cancelRequested.load()) {
            _db.setExtractionStatus(jobId, ExtractionStatus::Cancelled);
            _log.warn(QString("Job %1 extraction cancelled during section generation").arg(jobId));
            break;
        }

        {
            QMutexLocker locker(&_progressMutex);
            _progress.currentStep = extractedCount + skippedCount;
            _progress.currentGroupId = group.id;
            _progress.currentGroupLabel = group.label;
        }

        const GroupContext& ctx = contextByGroupId.value(group.id);
        if (ctx.assembledContext.isEmpty()) {
            _db.upsertReportSection(jobId, group.id, "{}", ReportSectionStatus::Failed);
            _log.info(QString("[Perf] Section '%1' skipped (zero context)").arg(group.id));
            skippedCount++;
            {
                QMutexLocker locker(&_progressMutex);
                _progress.currentStep = extractedCount + skippedCount;
            }
            continue;
        }

        qint64 prefillMs = 0;
        qint64 decodeMs = 0;
        int tokensGen = 0;
        generateAndPersistGroup(jobId, group, ctx, prefillMs, decodeMs, tokensGen, jobApiKey);

        totalPrefillMs += prefillMs;
        totalDecodeMs += decodeMs;
        totalTokens += tokensGen;
        extractedCount++;

        {
            QMutexLocker locker(&_progressMutex);
            _progress.currentStep = extractedCount + skippedCount;
        }
    }

    if (!isCloud) {
        _llama.scheduleIdleUnload(300);
    }

    if (_cancelRequested.load()) {
        _db.setExtractionStatus(jobId, ExtractionStatus::Cancelled);
        _log.warn(QString("Job %1 extraction terminated due to cancellation").arg(jobId));
        return;
    }

    _db.setExtractionStatus(jobId, ExtractionStatus::ReadyForReview);

    {
        QMutexLocker locker(&_progressMutex);
        _progress.currentStep = _catalog.groups().size();
        _progress.currentGroupLabel = "Extraction complete";
    }

    qint64 totalWallMs = totalJobTimer.elapsed();
    double totalWallSec = totalWallMs / 1000.0;
    double swapSec = (embdModelLoadMs + genModelLoadMs) / 1000.0;
    double retrievalSec = totalRetrievalMs / 1000.0;
    double prefillSec = totalPrefillMs / 1000.0;
    double decodeSec = totalDecodeMs / 1000.0;
    double avgTps = decodeSec > 0 ? (totalTokens / decodeSec) : 0.0;

    _log.info(QString(
        "\n======================================================================\n"
        "[Perf-Summary] Job %1 Extraction Completed in %2s (%16):\n"
        "  ├── Model Swapping:    %3s (%4%)\n"
        "  ├── Total Retrieval:   %5s (%6%)\n"
        "  ├── Total LLM Prefill: %7s (%8%)\n"
        "  ├── Total LLM Decode:  %9s (%10%) [Avg: %11 tokens/sec, %12 tokens total]\n"
        "  └── Total Sections:    %13 (%14 extracted, %15 skipped)\n"
        "======================================================================")
        .arg(jobId)
        .arg(totalWallSec, 0, 'f', 2)
        .arg(swapSec, 0, 'f', 2)
        .arg((swapSec / totalWallSec) * 100.0, 0, 'f', 1)
        .arg(retrievalSec, 0, 'f', 2)
        .arg((retrievalSec / totalWallSec) * 100.0, 0, 'f', 1)
        .arg(prefillSec, 0, 'f', 2)
        .arg((prefillSec / totalWallSec) * 100.0, 0, 'f', 1)
        .arg(decodeSec, 0, 'f', 2)
        .arg((decodeSec / totalWallSec) * 100.0, 0, 'f', 1)
        .arg(avgTps, 0, 'f', 1)
        .arg(totalTokens)
        .arg(_catalog.groups().size())
        .arg(extractedCount)
        .arg(skippedCount)
        .arg(isCloud ? "Google Cloud AI" : "Local Gemma 2"));
    }
    catch (const std::exception& e)
    {
        _llama.unloadEmbdModel();
        _llama.unloadGenModel();
        _db.setExtractionStatus(jobId, ExtractionStatus::Failed);
        _log.error(QString("Job %1 extraction failed with unhandled exception: %2").arg(jobId).arg(e.what()));
    }
    catch (...)
    {
        _llama.unloadEmbdModel();
        _llama.unloadGenModel();
        _db.setExtractionStatus(jobId, ExtractionStatus::Failed);
        _log.error(QString("Job %1 extraction failed with unknown exception").arg(jobId));
    }
}

ReportExtractionService::ProgressInfo ReportExtractionService::currentProgress() const
{
    QMutexLocker locker(&_progressMutex);
    return _progress;
}

