#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include "db/Database.h"
#include "inference/LlamaEngine.h"
#include "client/GeminiClient.h"
#include "logging/Logger.h"
#include "queue/JobQueue.h"
#include "report/BRSRSchema.h"
#include "report/ReportExporter.h"
#include "report/ReportExtractionService.h"
#include "report/ReportService.h"
#include "system/SystemMonitor.h"
#include "vector/VectorIndexRegistry.h"

#include <QObject>
#include <QHttpServer>
#include <QFuture>

class HttpServer : public QObject
{
    Q_OBJECT

public:
    explicit HttpServer(Database& db, LlamaEngine& llama, GeminiClient& gemini,
                        VectorIndexRegistry& indexRegistry,
                        SystemMonitor& monitor, const BRSRSchemaCatalog& catalog,
                        ReportExtractionService& extractionService, ReportExporter& exporter,
                        JobQueue& jobQueue,
                        QObject* parent = Q_NULLPTR);
    bool listen(quint16 port);

private:
    QFuture<QHttpServerResponse> handleIngest(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleJobStatus(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleSystemStatus(const QHttpServerRequest& request);

    QFuture<QHttpServerResponse> handleGetMode(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handlePostMode(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleTestConnection(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleModelsList(const QHttpServerRequest& request);

    QFuture<QHttpServerResponse> handleExtractionStart(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleExtractionStatus(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleExtractionCancel(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleExtractionSections(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleExtractionSectionUpdate(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleExtractionAccept(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleResolveExtractionRateLimit(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleResolveJobRateLimit(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleJobCancel(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleReportExport(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleReportsList(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleReportDelete(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleJobDocuments(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleGetSchemaBrsr(const QHttpServerRequest& request);
    QFuture<QHttpServerResponse> handleUpload(const QHttpServerRequest& request);

    QHttpServer               _server;
    Database&                 _db;
    LlamaEngine&              _llama;
    GeminiClient&             _gemini;
    VectorIndexRegistry&      _indexRegistry;
    SystemMonitor&            _monitor;
    const BRSRSchemaCatalog&  _catalog;
    ReportExtractionService&  _extractionService;
    ReportExporter&           _exporter;
    ReportService             _reportService;
    JobQueue&                 _jobQueue;
    Logger                    _log{"HttpServer"};
};

#endif // HTTPSERVER_H
