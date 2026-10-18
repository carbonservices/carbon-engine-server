#pragma once
#include <QString>
#include "BRSRSchema.h"
#include "db/Database.h"
#include "logging/Logger.h"

// Renders a job's accepted report_sections into the final BRSR HTML via template.html + Inja.
// This is the only place HTML rendering happens; it is a cheap re-render of already-locked
// data, so unlike the old handleReport there is nothing to cache.
class ReportExporter {
public:
    ReportExporter(Database& db, const BRSRSchemaCatalog& catalog);

    // Returns the rendered HTML, or an inline HTML error fragment if the job isn't locked,
    // the template is missing, or rendering fails for any other reason.
    // allowDraft: if true, allows rendering even if report is not yet accepted/locked (for live preview).
    QString exportJob(int jobId, bool allowDraft = false);

private:
    Database& _db;
    const BRSRSchemaCatalog& _catalog;
    Logger _log{"ReportExporter"};
};
