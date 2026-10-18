#include "ReportService.h"
#include "ReportValidator.h"
#include "constants/CommonConstants.h"

#include <QJsonDocument>

ReportService::ReportService(Database& db, const BRSRSchemaCatalog& catalog)
    : _db(db), _catalog(catalog)
{
}

QJsonObject ReportService::getExtractionSections(int jobId) const
{
    QVector<ReportSection> sections = _db.getReportSections(jobId);

    QJsonArray sectionsJson;
    for (const ReportSection& section : sections) {
        const FieldGroup* group = _catalog.findGroup(section.groupId);
        QJsonObject obj;
        obj["group_id"] = section.groupId;
        obj["label"] = group ? group->label : section.groupId;
        obj["section"] = group ? group->section : QString();
        obj["status"] = section.status;
        obj["data"] = QJsonDocument::fromJson(section.dataJson.toUtf8()).object();
        if (group) {
            obj["data_keys"] = QJsonArray::fromStringList(group->dataFieldKeys);
            obj["narrative_keys"] = QJsonArray::fromStringList(group->narrativeFieldKeys);
            obj["loop_keys"] = QJsonArray::fromStringList(group->loopFieldKeys);
        }
        sectionsJson.append(obj);
    }

    QVector<ValidationIssue> issues = ReportValidator::validateIssues(sections);
    QJsonArray issuesJson;
    for (const ValidationIssue& issue : issues) {
        QJsonObject iObj;
        iObj["group_id"] = issue.groupId;
        iObj["field_key"] = issue.fieldKey;
        iObj["message"] = issue.message;
        iObj["severity"] = issue.severity;
        issuesJson.append(iObj);
    }
    QStringList warnings = ReportValidator::validate(sections);

    return QJsonObject{
        {"sections", sectionsJson},
        {"warnings", QJsonArray::fromStringList(warnings)},
        {"issues", issuesJson},
        {"job_id", jobId}
    };
}

bool ReportService::updateSection(int jobId, const QString& groupId, const QJsonObject& data,
                                  QString& outError, bool& outForbidden)
{
    outForbidden = false;
    if (_db.isReportLocked(jobId)) {
        _log.error(QString("Rejected section edit for locked job %1").arg(jobId));
        outForbidden = true;
        outError = QString("Report for job %1 is locked and cannot be edited").arg(jobId);
        return false;
    }

    QString dataJson = QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
    if (!_db.upsertReportSection(jobId, groupId, dataJson, ReportSectionStatus::Updated)) {
        outError = "Failed to update report section in database";
        return false;
    }
    return true;
}

bool ReportService::acceptReport(int jobId, QString& outError)
{
    if (!_db.lockReport(jobId)) {
        outError = "Failed to lock report in database";
        return false;
    }
    _log.info(QString("Report accepted and locked for job %1").arg(jobId));
    return true;
}

QJsonArray ReportService::getJobDocumentsJson(int jobId) const
{
    auto docs = _db.getJobDocuments(jobId);
    QJsonArray docsArr;
    for (const auto& d : docs) {
        QJsonObject dObj;
        dObj["id"] = d.id;
        dObj["job_id"] = d.jobId;
        dObj["file_path"] = d.filePath;
        dObj["file_name"] = d.fileName;
        dObj["status"] = d.status;
        dObj["chunk_count"] = d.chunkCount;
        dObj["page_count"] = d.pageCount;
        docsArr.append(dObj);
    }
    return docsArr;
}

QJsonArray ReportService::listReports(int limit, int offset) const
{
    auto reports = _db.getAllReports();
    QJsonArray arr;
    int count = 0;
    int skipped = 0;

    for (const auto& r : reports) {
        if (offset > 0 && skipped < offset) {
            skipped++;
            continue;
        }
        if (limit > 0 && count >= limit) {
            break;
        }

        QJsonObject obj;
        obj["job_id"] = r.jobId;
        obj["title"] = r.title;
        obj["status"] = r.status;
        obj["extraction_status"] = r.extractionStatus;
        obj["report_locked"] = _db.isReportLocked(r.jobId);
        obj["date"] = r.createdAt.section(' ', 0, 0);
        obj["documents"] = getJobDocumentsJson(r.jobId);
        arr.append(obj);
        count++;
    }
    return arr;
}

