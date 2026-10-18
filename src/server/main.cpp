#include "ServerConfig.h"

#include "api/HTTPServer.h"
#include "db/Database.h"
#include "inference/LlamaEngine.h"
#include "client/GeminiClient.h"
#include "ingestion/DocumentProcessor.h"
#include "logging/Logger.h"
#include "queue/JobQueue.h"
#include "report/BRSRSchema.h"
#include "report/ReportExtractionService.h"
#include "report/ReportExporter.h"
#include "system/SystemMonitor.h"
#include "vector/VectorIndexRegistry.h"
#include "system/SingleInstanceGuard.h"
#include "system/WindowsService.h"

#include <QCoreApplication>
#include <iostream>

int runServerDaemon(int argc, char *argv[])
{
    SingleInstanceGuard guard("CarbonEngine_Daemon_Mutex");
    if (!guard.isPrimary())
    {
        Logger log("Carbon Engine");
        log.critical("Another instance of Carbon Engine is already running. Exiting.");
        std::cerr << "Another instance of Carbon Engine is already running. Exiting." << std::endl;
        return 1;
    }

    QCoreApplication app(argc, argv);
    app.setApplicationName("Carbon Engine");
    app.setOrganizationName("Carbon Services");
#ifdef CARBON_ENGINE_VERSION
    app.setApplicationVersion(CARBON_ENGINE_VERSION);
#else
    app.setApplicationVersion("1.0.0");
#endif
    Logger log("Carbon Engine");

    Database db(kDatabaseFilePath);
    if (!db.init())
        return -1;

    SystemMonitor monitor;

    LlamaEngine llama(kGenerationModelPath, kEmbeddingModelPath, monitor);
    if (!llama.initBackend())
    {
        log.critical("Failed to initialize Llama backend.");
        return -1;
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&llama, &log]() {
        log.info("Server shutting down, releasing inference resources...");
        llama.shutdown();
    });

    GeminiClient gemini;

    VectorIndexRegistry indexRegistry(kEmbeddingDim, &db);
    DocumentProcessor docProcessor;

    JobQueue jobQueue(db, docProcessor, llama, indexRegistry);
    jobQueue.start(1500);

    BRSRSchemaCatalog schemaCatalog;
    ReportExtractionService extractionService(db, llama, gemini, indexRegistry, schemaCatalog, monitor);
    ReportExporter exporter(db, schemaCatalog);

    QObject::connect(&jobQueue, &JobQueue::jobCompleted, [&extractionService, &log](int jobId) {
        log.info(QString("Job %1 ingestion complete. Automatically advancing to statutory extraction on backend...").arg(jobId));
        QThreadPool::globalInstance()->start([&extractionService, jobId]() {
            extractionService.extractJob(jobId);
        });
    });

    HttpServer server(db, llama, gemini, indexRegistry, monitor, schemaCatalog, extractionService, exporter, jobQueue);
    if (!server.listen(kDefaultPort))
    {
        log.critical(QString("Failed to bind HTTP server to port %1.").arg(kDefaultPort));
        return -1;
    }

    log.info(QString("Carbon Engine listening on http://localhost:%1").arg(kDefaultPort));
    return app.exec();
}

int main(int argc, char *argv[])
{
    if (WindowsService::handleServiceCommands(argc, argv, &runServerDaemon))
    {
        return 0;
    }
    return runServerDaemon(argc, argv);
}
