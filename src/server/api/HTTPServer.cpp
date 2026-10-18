#include "HTTPServer.h"
#include "constants/CommonConstants.h"
#include "report/ReportValidator.h"
#include "config/GCloudConfig.h"
#include "rate_limiter/RequestPacer.h"

// Qt
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThreadPool>
#include <QTcpServer>
#include <QHostAddress>
#include <QUrlQuery>
#include <QFileInfo>
#include <QHttpHeaders>
#include <QDateTime>
#include <QDir>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace {
QHttpServerResponse jsonError(QHttpServerResponder::StatusCode code, const QString &message)
{
    QJsonObject obj;
    obj["error"] = message;
    obj["status_code"] = static_cast<int>(code);
    return QHttpServerResponse(obj, code);
}

QString extractApiKey(const QHttpServerRequest &request, const QJsonObject &bodyObj = QJsonObject())
{
    // 1. Check custom headers: X-Gemini-API-Key or Authorization: Bearer <key>
    QByteArrayView customKey = request.headers().value("X-Gemini-API-Key");
    if (!customKey.isEmpty()) {
        QString key = QString::fromUtf8(customKey.toByteArray()).trimmed();
        if (!key.isEmpty()) return key;
    }

    QByteArrayView authHeader = request.headers().value("Authorization");
    if (!authHeader.isEmpty()) {
        QString authStr = QString::fromUtf8(authHeader.toByteArray()).trimmed();
        if (authStr.startsWith("Bearer ", Qt::CaseInsensitive)) {
            QString token = authStr.mid(7).trimmed();
            if (!token.isEmpty()) return token;
        }
    }

    // 2. Check JSON payload
    if (bodyObj.contains("api_key")) {
        QString key = bodyObj.value("api_key").toString().trimmed();
        if (!key.isEmpty()) return key;
    }
    return QString();
}
} // namespace

HttpServer::HttpServer(Database &db, LlamaEngine &llama, GeminiClient &gemini,
                       VectorIndexRegistry &indexRegistry,
                       SystemMonitor &monitor, const BRSRSchemaCatalog &catalog,
                       ReportExtractionService &extractionService, ReportExporter &exporter,
                       JobQueue &jobQueue,
                       QObject *parent)
    : QObject(parent), _db(db), _llama(llama), _gemini(gemini), _indexRegistry(indexRegistry), _monitor(monitor),
      _catalog(catalog), _extractionService(extractionService), _exporter(exporter),
      _reportService(db, catalog), _jobQueue(jobQueue)
{
    // Global CORS middleware: inject Access-Control headers into all responses
    _server.addAfterRequestHandler(this, [](const QHttpServerRequest &, QHttpServerResponse &resp) {
        QHttpHeaders headers = resp.headers();
        headers.replaceOrAppend("Access-Control-Allow-Origin", "*");
        headers.replaceOrAppend("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        headers.replaceOrAppend("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Gemini-API-Key");
        resp.setHeaders(std::move(headers));
    });

    // Global OPTIONS preflight and 404 JSON fallback handler
    _server.setMissingHandler(this, [](const QHttpServerRequest &req, QHttpServerResponder &responder) {
        if (req.method() == QHttpServerRequest::Method::Options) {
            QHttpServerResponse resp(QHttpServerResponder::StatusCode::NoContent);
            QHttpHeaders headers;
            headers.append("Access-Control-Allow-Origin", "*");
            headers.append("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            headers.append("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Gemini-API-Key");
            headers.append("Access-Control-Max-Age", "86400");
            resp.setHeaders(std::move(headers));
            responder.sendResponse(std::move(resp));
            return;
        }
        QJsonObject err;
        err["error"] = "Not Found";
        err["status_code"] = 404;
        QHttpServerResponse resp(err, QHttpServerResponder::StatusCode::NotFound);
        QHttpHeaders headers = resp.headers();
        headers.append("Access-Control-Allow-Origin", "*");
        resp.setHeaders(std::move(headers));
        responder.sendResponse(std::move(resp));
    });

    _server.route(ApiRoutes::Ingest, QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req)
                  { return handleIngest(req); });

    _server.route(ApiRoutes::Jobs, QHttpServerRequest::Method::Get,
                  [this](const QHttpServerRequest &req)
                  { return handleJobStatus(req); });

    _server.route(ApiRoutes::System, QHttpServerRequest::Method::Get,
                  [this](const QHttpServerRequest &req)
                  { return handleSystemStatus(req); });

    _server.route(ApiRoutes::Mode, QHttpServerRequest::Method::Get,
                  [this](const QHttpServerRequest &req)
                  { return handleGetMode(req); });

    _server.route(ApiRoutes::Mode, QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req)
                  { return handlePostMode(req); });

    _server.route(ApiRoutes::ConfigTest, QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req)
                  { return handleTestConnection(req); });

    _server.route(ApiRoutes::Models, QHttpServerRequest::Method::Get,
                  [this](const QHttpServerRequest &req)
                  { return handleModelsList(req); });

    _server.route(ApiRoutes::Models, QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req)
                  { return handleModelsList(req); });

    _server.route(ApiRoutes::ExtractionStart, QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req)
                  { return handleExtractionStart(req); });

    _server.route(ApiRoutes::ExtractionStatus, QHttpServerRequest::Method::Get,
                  [this](const QHttpServerRequest &req)
                  { return handleExtractionStatus(req); });

    _server.route(ApiRoutes::ExtractionCancel, QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req)
                  { return handleExtractionCancel(req); });

    _server.route(ApiRoutes::JobCancel, QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req)
                  { return handleJobCancel(req); });

    _server.route(ApiRoutes::ExtractionSections, QHttpServerRequest::Method::Get,
                  [this](const QHttpServerRequest &req)
                  { return handleExtractionSections(req); });

    _server.route(ApiRoutes::ExtractionSections, QHttpServerRequest::Method::Put,
                  [this](const QHttpServerRequest &req)
                  { return handleExtractionSectionUpdate(req); });

    _server.route(ApiRoutes::ExtractionAccept, QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req)
                  { return handleExtractionAccept(req); });

    _server.route(ApiRoutes::ReportExport, QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req)
                  { return handleReportExport(req); });

    _server.route(ApiRoutes::ExtractionResolveRateLimit, QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req)
                  { return handleResolveExtractionRateLimit(req); });

    _server.route(ApiRoutes::JobResolveRateLimit, QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req)
                  { return handleResolveJobRateLimit(req); });

    _server.route(ApiRoutes::Reports, QHttpServerRequest::Method::Get,
                  [this](const QHttpServerRequest &req)
                  { return handleReportsList(req); });

    _server.route(ApiRoutes::Reports, QHttpServerRequest::Method::Delete,
                  [this](const QHttpServerRequest &req)
                  { return handleReportDelete(req); });

    _server.route(ApiRoutes::Documents, QHttpServerRequest::Method::Get,
                  [this](const QHttpServerRequest &req)
                  { return handleJobDocuments(req); });

    _server.route(ApiRoutes::SchemaBrsr, QHttpServerRequest::Method::Get,
                  [this](const QHttpServerRequest &req)
                  { return handleGetSchemaBrsr(req); });

    _server.route("/api/upload", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req)
                  { return handleUpload(req); });

    auto serveLogo = [](const QHttpServerRequest &) {
        QFile file("logo.png");
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            QHttpServerResponse resp("image/png", data);
            QHttpHeaders headers = resp.headers();
            headers.append("Access-Control-Allow-Origin", "*");
            headers.append("Cache-Control", "public, max-age=86400");
            resp.setHeaders(std::move(headers));
            return resp;
        }
        return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
    };
    _server.route("/logo.png", QHttpServerRequest::Method::Get, serveLogo);
    _server.route("/api/logo", QHttpServerRequest::Method::Get, serveLogo);
}

bool HttpServer::listen(quint16 port)
{
    auto *tcpServer = new QTcpServer(this);
    bool bindAll = (qEnvironmentVariableIntValue("CARBON_ENGINE_BIND_ALL") == 1) ||
                   (!qEnvironmentVariableIsEmpty("K_SERVICE")) ||
                   (QFile::exists("/.dockerenv"));
    QHostAddress bindAddr = bindAll ? QHostAddress::Any : QHostAddress::LocalHost;
    if (!tcpServer->listen(bindAddr, port))
    {
        _log.error(QString("Failed to bind to port %1 on %2").arg(port).arg(bindAddr.toString()));
        delete tcpServer;
        return false;
    }

    bool bound = _server.bind(tcpServer);
    if (bound)
    {
        _log.info(QString("Listening on %1:%2").arg(bindAddr.toString()).arg(port));
    }
    return bound;
}

QFuture<QHttpServerResponse> HttpServer::handleIngest(const QHttpServerRequest &request)
{
    auto doc = QJsonDocument::fromJson(request.body());
    QJsonObject rootObj = doc.object();
    QJsonArray pathArray = rootObj.value("file_paths").toArray();

    QStringList filePaths;
    for (const QJsonValue& value : pathArray) {
        if (value.isString() && !value.toString().trimmed().isEmpty()) {
            filePaths.append(value.toString().trimmed());
        }
    }

    // Support embedded base64 files array for direct cloud upload & ingestion
    if (rootObj.contains("files") && rootObj.value("files").isArray()) {
        QString uploadDir = QProcessEnvironment::systemEnvironment().value("CARBON_ENGINE_UPLOAD_DIR");
        if (uploadDir.isEmpty()) {
            uploadDir = QDir("/app/data").exists() ? "/app/data/uploads" : (QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/CarbonESG/uploads");
        }
        QDir().mkpath(uploadDir);

        QJsonArray filesArr = rootObj.value("files").toArray();
        for (const QJsonValue& fVal : filesArr) {
            if (!fVal.isObject()) continue;
            QJsonObject fObj = fVal.toObject();
            QString filename = QFileInfo(fObj.value("filename").toString("document.pdf")).fileName();
            QByteArray content = QByteArray::fromBase64(fObj.value("data").toString().toLatin1());
            if (!content.isEmpty()) {
                QString targetPath = uploadDir + "/" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "_" + filename;
                QFile outFile(targetPath);
                if (outFile.open(QIODevice::WriteOnly)) {
                    outFile.write(content);
                    outFile.close();
                    filePaths.append(targetPath);
                }
            }
        }
    }

    if (filePaths.isEmpty()) {
        _log.error("Ingest request missing valid file_paths or files array");
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest,
            "Ingest request missing valid 'file_paths' or 'files' array"));
    }

    // Verify all specified files exist on the filesystem before creating the job
    for (const QString& path : filePaths) {
        QFileInfo fi(path);
        if (!fi.exists() || !fi.isFile()) {
            _log.error(QString("Ingest file not found on disk: %1").arg(path));
            return QtFuture::makeReadyFuture(jsonError(
                QHttpServerResponder::StatusCode::BadRequest,
                QString("File not found on disk: %1").arg(path)));
        }
    }

    QString apiKey = extractApiKey(request, rootObj);
    int jobId = _db.enqueueJob(filePaths, apiKey);
    if (jobId < 0) {
        _log.error(QString("Failed to enqueue job for %1 document(s)").arg(filePaths.size()));
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::InternalServerError,
            "Failed to persist ingestion job to database"));
    }

    _log.info(QString("Job %1 queued for %2 document(s)").arg(jobId).arg(filePaths.size()));
    _jobQueue.notifyNewJob();
    return QtFuture::makeReadyFuture(QHttpServerResponse(QJsonObject{{"status", "QUEUED"}, {"job_id", jobId}}));
}

QFuture<QHttpServerResponse> HttpServer::handleUpload(const QHttpServerRequest &request)
{
    auto doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject()) {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest, "Invalid JSON body for upload"));
    }

    QString uploadDir = QProcessEnvironment::systemEnvironment().value("CARBON_ENGINE_UPLOAD_DIR");
    if (uploadDir.isEmpty()) {
        uploadDir = QDir("/app/data").exists() ? "/app/data/uploads" : (QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/CarbonESG/uploads");
    }
    QDir().mkpath(uploadDir);

    QJsonObject rootObj = doc.object();
    QJsonArray filesToProcess;
    if (rootObj.contains("files") && rootObj.value("files").isArray()) {
        filesToProcess = rootObj.value("files").toArray();
    } else if (rootObj.contains("filename") && rootObj.contains("data")) {
        filesToProcess.append(rootObj);
    } else {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest, "Upload requires 'filename' & 'data' (base64) or 'files' array"));
    }

    QJsonArray uploadedArr;
    for (const QJsonValue& val : filesToProcess) {
        if (!val.isObject()) continue;
        QJsonObject fObj = val.toObject();
        QString origName = QFileInfo(fObj.value("filename").toString("document.pdf")).fileName();
        QByteArray content = QByteArray::fromBase64(fObj.value("data").toString().toLatin1());
        if (content.isEmpty()) continue;

        QString targetPath = uploadDir + "/" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "_" + origName;
        QFile outFile(targetPath);
        if (outFile.open(QIODevice::WriteOnly)) {
            outFile.write(content);
            outFile.close();

            QJsonObject uploadedItem;
            uploadedItem["filename"] = origName;
            uploadedItem["file_path"] = targetPath;
            uploadedItem["size_bytes"] = content.size();
            uploadedArr.append(uploadedItem);
        }
    }

    if (uploadedArr.isEmpty()) {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest, "No files could be processed or decoded"));
    }

    QJsonObject resp;
    resp["status"] = "SUCCESS";
    resp["uploaded_files"] = uploadedArr;
    return QtFuture::makeReadyFuture(QHttpServerResponse(resp));
}

QFuture<QHttpServerResponse> HttpServer::handleJobStatus(const QHttpServerRequest &request)
{
    bool ok = false;
    int jobId = request.query().queryItemValue("job_id").toInt(&ok);

    if (!ok || jobId < 0)
    {
        _log.error("Job status request missing job_id query parameter");
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest,
            "Missing or invalid 'job_id' query parameter"));
    }

    QString status = _db.getJobStatus(jobId);
    if (status.isEmpty())
    {
        _log.error(QString("Job status requested for unknown job %1").arg(jobId));
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::NotFound,
            QString("Job %1 not found").arg(jobId)));
    }

    QJsonObject obj;
    obj["job_id"] = jobId;
    obj["status"] = status;
    obj["extraction_status"] = _db.getExtractionStatus(jobId);
    obj["report_locked"] = _db.isReportLocked(jobId);
    obj["documents"] = _reportService.getJobDocumentsJson(jobId);
    obj["rate_limited"] = _jobQueue.isRateLimited();
    if (_jobQueue.isRateLimited()) {
        obj["rate_limit_error"] = _jobQueue.rateLimitError();
        obj["rate_limited_model"] = _jobQueue.rateLimitedModel();
        obj["rate_limit_type"] = "EMBEDDING";
    }

    return QtFuture::makeReadyFuture(QHttpServerResponse(obj));
}

QFuture<QHttpServerResponse> HttpServer::handleSystemStatus(const QHttpServerRequest& /*request*/)
{
    QJsonObject obj = _monitor.getSystemSnapshot();
    obj["gpu_offload_mode"]     = _llama.offloadModeString();
    obj["gpu_layers_offloaded"] = _llama.activeGenGpuLayers();
    obj["job_queue_depth"]      = _db.getPendingJobCount();

    quint64 dbBytes             = _db.diskSizeBytes();
    double dbMb                 = static_cast<double>(dbBytes) / (1024.0 * 1024.0);
    int totalChunks             = _db.totalChunkCount();
    obj["db_status"]            = totalChunks > 0
                                  ? QString("%1 MB (%2 chunks)").arg(QString::number(dbMb, 'f', 1)).arg(totalChunks)
                                  : QString("%1 MB").arg(QString::number(dbMb, 'f', 1));

    bool cloudEnabled           = GCloudConfig::instance().isCloudEnabled();
    obj["cloud_enabled"]        = cloudEnabled;
    obj["cloud_model"]          = GCloudConfig::instance().modelName();
    obj["active_engine_mode"]   = cloudEnabled ? EngineMode::GCloud : EngineMode::Offline;
    obj["active_model"]         = cloudEnabled ? GCloudConfig::instance().modelName() : Models::LocalGemmaName;
    obj["cloud_pacing_status"]  = RequestPacer::instance().currentPacingStatus();
    obj["api_key_configured"]   = !GCloudConfig::instance().apiKey().isEmpty();

    return QtFuture::makeReadyFuture(QHttpServerResponse(obj));
}

QFuture<QHttpServerResponse> HttpServer::handleGetMode(const QHttpServerRequest &)
{
    QJsonObject obj;
    bool cloudEnabled = GCloudConfig::instance().isCloudEnabled();
    obj["mode"] = cloudEnabled ? EngineMode::GCloud : EngineMode::Offline;
    obj["cloud_enabled"] = cloudEnabled;
    obj["api_key_configured"] = !GCloudConfig::instance().apiKey().isEmpty();
    obj["api_key"] = GCloudConfig::instance().apiKey();
    obj["cloud_model"] = GCloudConfig::instance().modelName();
    obj["cloud_embedding_model"] = GCloudConfig::instance().embeddingModelName();
    obj["available_models"] = QJsonArray::fromStringList(GCloudConfig::instance().availableModels());
    obj["available_embedding_models"] = QJsonArray::fromStringList(GCloudConfig::instance().availableEmbeddingModels());
    obj["local_model"] = Models::LocalGemmaName;
    obj["disclaimer"] = GCloudConfig::instance().dataProcessingDisclaimer();
    obj["pacing_status"] = RequestPacer::instance().currentPacingStatus();
    return QtFuture::makeReadyFuture(QHttpServerResponse(obj));
}

QFuture<QHttpServerResponse> HttpServer::handlePostMode(const QHttpServerRequest &request)
{
    auto doc = QJsonDocument::fromJson(request.body());
    QJsonObject obj = doc.object();

    if (obj.contains("mode")) {
        QString modeStr = obj.value("mode").toString();
        GCloudConfig::instance().setCloudEnabled(modeStr.compare(EngineMode::GCloud, Qt::CaseInsensitive) == 0);
    }
    if (obj.contains("cloud_enabled")) {
        GCloudConfig::instance().setCloudEnabled(obj.value("cloud_enabled").toBool());
    }
    if (obj.contains("api_key")) {
        GCloudConfig::instance().setApiKey(obj.value("api_key").toString());
    }
    if (obj.contains("cloud_model")) {
        GCloudConfig::instance().setModelName(obj.value("cloud_model").toString());
    }
    if (obj.contains("cloud_embedding_model")) {
        GCloudConfig::instance().setEmbeddingModelName(obj.value("cloud_embedding_model").toString());
    } else if (obj.contains("embedding_model")) {
        GCloudConfig::instance().setEmbeddingModelName(obj.value("embedding_model").toString());
    }

    QJsonObject resp;
    resp["status"] = "UPDATED";
    resp["mode"] = GCloudConfig::instance().isCloudEnabled() ? EngineMode::GCloud : EngineMode::Offline;
    resp["cloud_enabled"] = GCloudConfig::instance().isCloudEnabled();
    resp["cloud_model"] = GCloudConfig::instance().modelName();
    resp["cloud_embedding_model"] = GCloudConfig::instance().embeddingModelName();
    return QtFuture::makeReadyFuture(QHttpServerResponse(resp));
}

QFuture<QHttpServerResponse> HttpServer::handleTestConnection(const QHttpServerRequest &request)
{
    QString overrideApiKey;
    QString overrideModel;
    QString overrideEmbeddingModel;

    if (!request.body().isEmpty()) {
        auto doc = QJsonDocument::fromJson(request.body());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            overrideApiKey = obj.value("api_key").toString().trimmed();
            overrideModel = obj.value("cloud_model").toString().trimmed();
            overrideEmbeddingModel = obj.value("cloud_embedding_model").toString().trimmed();
            if (overrideEmbeddingModel.isEmpty()) {
                overrideEmbeddingModel = obj.value("embedding_model").toString().trimmed();
            }
        }
    }

    if (overrideApiKey.isEmpty()) {
        overrideApiKey = extractApiKey(request);
    }

    QString model;
    qint64 latencyMs = 0;
    QString error;
    bool ok = _gemini.testConnection(model, latencyMs, error, overrideApiKey, overrideModel);

    QString embModel;
    qint64 embLatencyMs = 0;
    int embDim = 0;
    QString embError;
    bool embOk = _gemini.testEmbeddingConnection(embModel, embLatencyMs, embDim, embError, overrideApiKey, overrideEmbeddingModel);

    QJsonObject resp;
    resp["success"] = ok && embOk;
    resp["inference_success"] = ok;
    resp["model"] = model;
    resp["latency_ms"] = latencyMs;
    resp["embedding_success"] = embOk;
    resp["embedding_model"] = embModel;
    resp["embedding_latency_ms"] = embLatencyMs;
    resp["embedding_dim"] = embDim;
    resp["available_models"] = QJsonArray::fromStringList(GCloudConfig::instance().availableModels());
    resp["available_embedding_models"] = QJsonArray::fromStringList(GCloudConfig::instance().availableEmbeddingModels());
    if (!ok) {
        resp["error"] = QString("Inference test failed: %1").arg(error);
    } else if (!embOk) {
        resp["error"] = QString("Embedding test failed: %1").arg(embError);
    }
    return QtFuture::makeReadyFuture(QHttpServerResponse(resp));
}

QFuture<QHttpServerResponse> HttpServer::handleModelsList(const QHttpServerRequest &request)
{
    QString apiKey;
    if (!request.body().isEmpty()) {
        auto doc = QJsonDocument::fromJson(request.body());
        if (doc.isObject()) {
            apiKey = doc.object().value("api_key").toString().trimmed();
        }
    }
    if (apiKey.isEmpty()) {
        apiKey = extractApiKey(request);
    }
    if (apiKey.isEmpty()) {
        apiKey = GCloudConfig::instance().apiKey();
    }
    QString err;
    if (!apiKey.isEmpty()) {
        _gemini.fetchAvailableModels(apiKey, err);
    }
    QJsonObject resp;
    resp["available_models"] = QJsonArray::fromStringList(GCloudConfig::instance().availableModels());
    resp["available_embedding_models"] = QJsonArray::fromStringList(GCloudConfig::instance().availableEmbeddingModels());
    if (!err.isEmpty()) {
        resp["error"] = err;
    }
    return QtFuture::makeReadyFuture(QHttpServerResponse(resp));
}

QFuture<QHttpServerResponse> HttpServer::handleExtractionStart(const QHttpServerRequest &request)
{
    auto doc = QJsonDocument::fromJson(request.body());
    QJsonObject rootObj = doc.object();
    int jobId = rootObj.value("job_id").toInt(-1);

    if (jobId < 0) {
        _log.error("Extraction start request missing valid job_id");
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest,
            "Extraction start request missing valid 'job_id'"));
    }

    QString jobStatus = _db.getJobStatus(jobId);
    if (jobStatus.isEmpty()) {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::NotFound,
            QString("Job %1 not found").arg(jobId)));
    }

    if (jobStatus != JobStatus::Completed) {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest,
            QString("Cannot extract job %1 with status '%2' (ingestion must be COMPLETED)").arg(jobId).arg(jobStatus)));
    }

    if (_extractionService.isExtracting()) {
        _log.warn(QString("Extraction already in progress for job %1. Rejecting request for job %2.")
                      .arg(_extractionService.activeJobId()).arg(jobId));
        QJsonObject errObj;
        errObj["error"] = "Extraction already in progress";
        errObj["active_job_id"] = _extractionService.activeJobId();
        errObj["status_code"] = static_cast<int>(QHttpServerResponder::StatusCode::Conflict);
        return QtFuture::makeReadyFuture(QHttpServerResponse(errObj, QHttpServerResponder::StatusCode::Conflict));
    }

    QString apiKey = extractApiKey(request, rootObj);
    if (!apiKey.isEmpty()) {
        _db.setJobApiKey(jobId, apiKey);
    }

    _log.info(QString("Starting extraction for job %1").arg(jobId));
    QThreadPool::globalInstance()->start([this, jobId, apiKey]() { _extractionService.extractJob(jobId, apiKey); });
    return QtFuture::makeReadyFuture(QHttpServerResponse(QJsonObject{{"status", ExtractionStatus::Extracting}, {"job_id", jobId}}));
}

QFuture<QHttpServerResponse> HttpServer::handleExtractionStatus(const QHttpServerRequest &request)
{
    bool ok = false;
    int jobId = request.query().queryItemValue("job_id").toInt(&ok);

    if (!ok || jobId < 0) {
        _log.error("Extraction status request missing job_id query parameter");
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest,
            "Missing or invalid 'job_id' query parameter"));
    }

    QString status = _db.getExtractionStatus(jobId);
    if (status.isEmpty()) {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::NotFound,
            QString("Job %1 not found").arg(jobId)));
    }

    QJsonObject resp;
    resp["job_id"] = jobId;
    resp["status"] = status;

    bool cloudEnabled = GCloudConfig::instance().isCloudEnabled();
    resp["engine_mode"] = cloudEnabled ? EngineMode::GCloud : EngineMode::Offline;
    resp["active_model"] = cloudEnabled ? GCloudConfig::instance().modelName() : Models::LocalGemmaName;
    resp["cloud_pacing_status"] = RequestPacer::instance().currentPacingStatus();
    resp["last_latency_ms"] = _gemini.lastLatencyMs();
    resp["last_prompt_tokens"] = _gemini.lastPromptTokens();
    resp["last_candidate_tokens"] = _gemini.lastCandidateTokens();

    auto progress = _extractionService.currentProgress();
    if (_extractionService.activeJobId() == jobId || progress.currentStep > 0) {
        resp["current_step"] = progress.currentStep;
        resp["total_steps"] = progress.totalSteps;
        resp["current_group"] = progress.currentGroupLabel;
        resp["current_group_id"] = progress.currentGroupId;
        double pct = (progress.totalSteps > 0)
            ? (static_cast<double>(progress.currentStep) / progress.totalSteps * 100.0)
            : 0.0;
        resp["progress_pct"] = qRound(pct * 10.0) / 10.0;

        QJsonArray missingArr;
        for (const auto& sec : progress.missingSections) {
            QJsonObject sObj;
            sObj["group_id"] = sec.groupId;
            sObj["group_label"] = sec.groupLabel;
            sObj["missing_keys"] = QJsonArray::fromStringList(sec.missingKeys);
            missingArr.append(sObj);
        }
        resp["missing_metrics"] = missingArr;
    }

    resp["rate_limited"] = progress.isRateLimited;
    if (progress.isRateLimited) {
        resp["rate_limit_error"] = progress.rateLimitError;
        resp["rate_limited_model"] = progress.rateLimitedModel;
        resp["rate_limited_section"] = progress.rateLimitedSection;
        resp["rate_limit_type"] = progress.rateLimitType;
    }

    return QtFuture::makeReadyFuture(QHttpServerResponse(resp));
}

QFuture<QHttpServerResponse> HttpServer::handleExtractionSections(const QHttpServerRequest &request)
{
    bool ok = false;
    int jobId = request.query().queryItemValue("job_id").toInt(&ok);

    if (!ok || jobId < 0) {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest,
            "Missing or invalid 'job_id' query parameter"));
    }

    if (_db.getJobStatus(jobId).isEmpty()) {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::NotFound,
            QString("Job %1 not found").arg(jobId)));
    }

    return QtFuture::makeReadyFuture(QHttpServerResponse(_reportService.getExtractionSections(jobId)));
}

QFuture<QHttpServerResponse> HttpServer::handleExtractionSectionUpdate(const QHttpServerRequest &request)
{
    auto doc = QJsonDocument::fromJson(request.body());
    int jobId = doc.object().value("job_id").toInt(-1);
    QString groupId = doc.object().value("group_id").toString();
    QJsonObject data = doc.object().value("data").toObject();

    if (jobId < 0 || groupId.isEmpty()) {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest,
            "Missing valid 'job_id' or 'group_id' in request body"));
    }

    QString errMsg;
    bool forbidden = false;
    if (!_reportService.updateSection(jobId, groupId, data, errMsg, forbidden)) {
        return QtFuture::makeReadyFuture(jsonError(
            forbidden ? QHttpServerResponder::StatusCode::Forbidden
                      : QHttpServerResponder::StatusCode::InternalServerError,
            errMsg));
    }

    return QtFuture::makeReadyFuture(QHttpServerResponse(QJsonObject{{"status", ReportSectionStatus::Updated}, {"group_id", groupId}}));
}

QFuture<QHttpServerResponse> HttpServer::handleExtractionAccept(const QHttpServerRequest &request)
{
    auto doc = QJsonDocument::fromJson(request.body());
    int jobId = doc.object().value("job_id").toInt(-1);

    if (jobId < 0) {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest,
            "Extraction accept request missing valid 'job_id'"));
    }

    QString errMsg;
    if (!_reportService.acceptReport(jobId, errMsg)) {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::InternalServerError,
            errMsg));
    }
    return QtFuture::makeReadyFuture(QHttpServerResponse(QJsonObject{{"status", ExtractionStatus::Accepted}, {"job_id", jobId}}));
}

QFuture<QHttpServerResponse> HttpServer::handleReportExport(const QHttpServerRequest &request)
{
    auto doc = QJsonDocument::fromJson(request.body());
    int jobId = doc.object().value("job_id").toInt(-1);
    bool allowDraft = doc.object().value("draft").toBool(true);

    if (jobId < 0) {
        _log.error("Report export request missing job_id");
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest,
            "Report export request missing valid 'job_id'"));
    }

    bool isLocked = _db.isReportLocked(jobId);
    QString html = _exporter.exportJob(jobId, allowDraft || isLocked);
    QJsonObject resp;
    resp["report"] = html;
    resp["job_id"] = jobId;
    resp["is_locked"] = isLocked;
    return QtFuture::makeReadyFuture(QHttpServerResponse(resp));
}

QFuture<QHttpServerResponse> HttpServer::handleReportsList(const QHttpServerRequest &request)
{
    bool ok = false;
    int limit = request.query().queryItemValue("limit").toInt(&ok);
    int offset = request.query().queryItemValue("offset").toInt();

    return QtFuture::makeReadyFuture(QHttpServerResponse(_reportService.listReports(ok ? limit : 0, offset)));
}

QFuture<QHttpServerResponse> HttpServer::handleReportDelete(const QHttpServerRequest &request)
{
    bool ok = false;
    int jobId = request.query().queryItemValue("job_id").toInt(&ok);
    if (!ok || jobId < 0) {
        _log.error("Report delete request missing valid job_id");
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest,
            "Missing or invalid 'job_id' query parameter"));
    }

    // Cancel active ingestion or extraction before deletion
    _jobQueue.cancelJob(jobId);
    _extractionService.cancelJob(jobId);
    int waitCount = 0;
    while ((_jobQueue.currentJobId() == jobId || _extractionService.activeJobId() == jobId) && waitCount < 10) {
        QThread::msleep(100);
        waitCount++;
    }
    _extractionService.factStore().clearForJob(jobId);

    bool deleted = _db.deleteJob(jobId);
    if (!deleted) {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::InternalServerError,
            QString("Failed to delete job %1 from database").arg(jobId)));
    }
    _indexRegistry.remove(jobId);
    _log.info(QString("Successfully deleted report / job %1").arg(jobId));
    return QtFuture::makeReadyFuture(QHttpServerResponse(QJsonObject{{"status", "DELETED"}, {"job_id", jobId}}));
}

QFuture<QHttpServerResponse> HttpServer::handleJobDocuments(const QHttpServerRequest &request)
{
    bool ok = false;
    int jobId = request.query().queryItemValue("job_id").toInt(&ok);

    if (!ok || jobId < 0) {
        _log.error("Job documents request missing job_id query parameter");
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest,
            "Missing or invalid 'job_id' query parameter"));
    }

    if (_db.getJobStatus(jobId).isEmpty()) {
        _log.error(QString("Job documents requested for unknown job %1").arg(jobId));
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::NotFound,
            QString("Job %1 not found").arg(jobId)));
    }

    return QtFuture::makeReadyFuture(QHttpServerResponse(QJsonObject{
        {"job_id", jobId},
        {"documents", _reportService.getJobDocumentsJson(jobId)}
    }));
}

QFuture<QHttpServerResponse> HttpServer::handleResolveExtractionRateLimit(const QHttpServerRequest &request)
{
    auto doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject()) {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest, "Invalid JSON body"));
    }

    QJsonObject obj = doc.object();
    int jobId = obj.value("job_id").toInt(-1);
    QString action = obj.value("action").toString().trimmed().toLower();
    QString newModel = obj.value("new_model").toString().trimmed();

    if (action == "switch_model") {
        if (newModel.isEmpty()) {
            return QtFuture::makeReadyFuture(jsonError(
                QHttpServerResponder::StatusCode::BadRequest, "Missing 'new_model' for switch_model action"));
        }
        _extractionService.resolveRateLimit(ReportExtractionService::RateLimitResolution::SwitchCloudModel, newModel);
    } else if (action == "switch_local") {
        _extractionService.resolveRateLimit(ReportExtractionService::RateLimitResolution::SwitchToLocal);
    } else if (action == "retry") {
        _extractionService.resolveRateLimit(ReportExtractionService::RateLimitResolution::Retry);
    } else if (action == "cancel") {
        _extractionService.resolveRateLimit(ReportExtractionService::RateLimitResolution::Cancel);
    } else {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest, "Unknown action: " + action));
    }

    return QtFuture::makeReadyFuture(QHttpServerResponse(QJsonObject{
        {"status", "RESOLVED"},
        {"job_id", jobId},
        {"action", action}
    }));
}

QFuture<QHttpServerResponse> HttpServer::handleResolveJobRateLimit(const QHttpServerRequest &request)
{
    auto doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject()) {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest, "Invalid JSON body"));
    }

    QJsonObject obj = doc.object();
    int jobId = obj.value("job_id").toInt(-1);
    QString action = obj.value("action").toString().trimmed().toLower();
    QString newModel = obj.value("new_model").toString().trimmed();

    if (action == "switch_model") {
        if (newModel.isEmpty()) {
            return QtFuture::makeReadyFuture(jsonError(
                QHttpServerResponder::StatusCode::BadRequest, "Missing 'new_model' for switch_model action"));
        }
        _jobQueue.resolveRateLimit(JobQueue::RateLimitResolution::SwitchEmbeddingModel, newModel);
    } else if (action == "switch_local") {
        _jobQueue.resolveRateLimit(JobQueue::RateLimitResolution::SwitchToLocal);
    } else if (action == "retry") {
        _jobQueue.resolveRateLimit(JobQueue::RateLimitResolution::Retry);
    } else if (action == "cancel") {
        _jobQueue.resolveRateLimit(JobQueue::RateLimitResolution::Cancel);
    } else {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest, "Unknown action: " + action));
    }

    return QtFuture::makeReadyFuture(QHttpServerResponse(QJsonObject{
        {"status", "RESOLVED"},
        {"job_id", jobId},
        {"action", action}
    }));
}

QFuture<QHttpServerResponse> HttpServer::handleExtractionCancel(const QHttpServerRequest &request)
{
    auto doc = QJsonDocument::fromJson(request.body());
    int jobId = doc.object().value("job_id").toInt(-1);
    if (jobId < 0) {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest, "Missing valid 'job_id' parameter"));
    }

    _log.warn(QString("Cancellation requested for extraction job %1").arg(jobId));
    _extractionService.cancelJob(jobId);
    _db.setExtractionStatus(jobId, ExtractionStatus::Cancelled);

    return QtFuture::makeReadyFuture(QHttpServerResponse(QJsonObject{
        {"status", "CANCELLED"},
        {"job_id", jobId},
        {"message", "Extraction halted. All in-flight API calls cancelled."}
    }));
}

QFuture<QHttpServerResponse> HttpServer::handleJobCancel(const QHttpServerRequest &request)
{
    auto doc = QJsonDocument::fromJson(request.body());
    int jobId = doc.object().value("job_id").toInt(-1);
    if (jobId < 0) {
        return QtFuture::makeReadyFuture(jsonError(
            QHttpServerResponder::StatusCode::BadRequest, "Missing valid 'job_id' parameter"));
    }

    _log.warn(QString("Cancellation requested for document ingestion job %1").arg(jobId));
    _jobQueue.cancelJob(jobId);
    _db.updateJobStatus(jobId, JobStatus::Cancelled);

    return QtFuture::makeReadyFuture(QHttpServerResponse(QJsonObject{
        {"status", "CANCELLED"},
        {"job_id", jobId},
        {"message", "Ingestion halted. All in-flight API calls cancelled."}
    }));
}

QFuture<QHttpServerResponse> HttpServer::handleGetSchemaBrsr(const QHttpServerRequest &request)
{
    Q_UNUSED(request);
    return QtFuture::makeReadyFuture(QHttpServerResponse(_catalog.schemaMetadata()));
}

