#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include "db/Database.h"
#include "report/BRSRSchema.h"
#include "logging/Logger.h"

// Domain service encapsulating BRSR report lifecycle operations:
// section assembly + validation, section updates with audit lock enforcement,
// report acceptance/locking, report history listing, and job document serialization.
class ReportService {
public:
    ReportService(Database& db, const BRSRSchemaCatalog& catalog);

    QJsonObject getExtractionSections(int jobId) const;
    bool updateSection(int jobId, const QString& groupId, const QJsonObject& data,
                       QString& outError, bool& outForbidden);
    bool acceptReport(int jobId, QString& outError);
    QJsonArray listReports(int limit = 0, int offset = 0) const;
    QJsonArray getJobDocumentsJson(int jobId) const;

private:
    Database& _db;
    const BRSRSchemaCatalog& _catalog;
    mutable Logger _log{"ReportService"};
};

