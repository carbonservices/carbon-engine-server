#pragma once
#include <QStringList>
#include <QVector>
#include "db/Database.h"

// Flags issues in a job's extracted report sections before the user accepts them: missing
// required fields, out-of-range percentages, and simple cross-field consistency checks.
// Produces plain warning strings for the review UI, not hard errors — accepting a report
// with warnings is a user decision, not something this class enforces.
struct ValidationIssue {
    QString groupId;
    QString fieldKey;
    QString message;
    QString severity{"WARNING"};
};

class ReportValidator {
public:
    static QVector<ValidationIssue> validateIssues(const QVector<ReportSection>& sections);
    static QStringList validate(const QVector<ReportSection>& sections);
};
