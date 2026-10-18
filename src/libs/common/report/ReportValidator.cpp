#include "ReportValidator.h"
#include <QString>
#include <nlohmann/json.hpp>
#include <cmath>

namespace {

bool tryParseNumber(const nlohmann::json& value, double& outNumber)
{
    if (value.is_number()) {
        outNumber = value.get<double>();
        return true;
    }
    if (value.is_string()) {
        try {
            std::string s = value.get<std::string>();
            std::string clean;
            for (char c : s) {
                if ((c >= '0' && c <= '9') || c == '.' || c == '-') {
                    clean += c;
                }
            }
            if (!clean.empty()) {
                outNumber = std::stod(clean);
                return true;
            }
        } catch (...) {
            return false;
        }
    }
    return false;
}

void checkMissingAndRangeIssues(const QString& groupId, const nlohmann::json& data, QVector<ValidationIssue>& issues)
{
    for (auto& [key, value] : data.items()) {
        QString fieldName = QString::fromStdString(key);

        bool isMissing = value.is_null();
        if (!isMissing && value.is_string()) {
            std::string s = value.get<std::string>();
            isMissing = (s.empty() || s == "N/A" || s == "<Not provided>" || s == "null");
        } else if (!isMissing && value.is_array()) {
            isMissing = value.empty();
        }

        if (isMissing) {
            ValidationIssue issue;
            issue.groupId = groupId;
            issue.fieldKey = fieldName;
            issue.message = value.is_array()
                ? QString("table \"%1\" has no extracted records").arg(fieldName)
                : QString("field \"%1\" has no extracted value").arg(fieldName);
            issue.severity = "WARNING";
            issues.append(issue);
            continue;
        }

        if (fieldName.endsWith("_pct")) {
            double number = 0.0;
            if (tryParseNumber(value, number) && (number < 0.0 || number > 100.0)) {
                ValidationIssue issue;
                issue.groupId = groupId;
                issue.fieldKey = fieldName;
                issue.message = QString("field \"%1\" is %2, outside expected 0-100%% range")
                    .arg(fieldName).arg(number);
                issue.severity = "WARNING";
                issues.append(issue);
            }
        }
    }
}

void checkHeadcountGenderSum(const QString& groupId, const nlohmann::json& data,
                            const char* maleKey, const char* femaleKey, const char* totalKey,
                            const QString& label, QVector<ValidationIssue>& issues)
{
    if (data.contains(maleKey) && data.contains(femaleKey) && data.contains(totalKey)) {
        double male = 0.0;
        double female = 0.0;
        double total = 0.0;
        if (tryParseNumber(data[maleKey], male) && tryParseNumber(data[femaleKey], female) && tryParseNumber(data[totalKey], total)) {
            if (total > 0.0 && std::abs((male + female) - total) > 1.0) {
                ValidationIssue issue;
                issue.groupId = groupId;
                issue.fieldKey = QString::fromLatin1(totalKey);
                issue.message = QString("%1 headcount mismatch: male (%2) + female (%3) != total (%4)")
                    .arg(label).arg(male).arg(female).arg(total);
                issue.severity = "WARNING";
                issues.append(issue);
            }
        }
    }
}

void checkWorkforceConsistency(const QString& groupId, const nlohmann::json& data, QVector<ValidationIssue>& issues)
{
    // Employee percentage sum
    if (data.contains("emp_total_male_pct") && data.contains("emp_total_female_pct")) {
        double malePct = 0.0;
        double femalePct = 0.0;
        if (tryParseNumber(data["emp_total_male_pct"], malePct) && tryParseNumber(data["emp_total_female_pct"], femalePct)) {
            double total = malePct + femalePct;
            if (total < 95.0 || total > 105.0) {
                ValidationIssue issue;
                issue.groupId = groupId;
                issue.fieldKey = "emp_total_male_pct";
                issue.message = QString("employee gender breakdown sum (%1% + %2% = %3%) deviates from 100%")
                    .arg(malePct).arg(femalePct).arg(total);
                issue.severity = "WARNING";
                issues.append(issue);
            }
        }
    }

    // Employee permanent + other vs total
    if (data.contains("emp_perm_total") && data.contains("emp_other_total") && data.contains("emp_total_total")) {
        double perm = 0.0;
        double other = 0.0;
        double total = 0.0;
        if (tryParseNumber(data["emp_perm_total"], perm) && tryParseNumber(data["emp_other_total"], other) && tryParseNumber(data["emp_total_total"], total)) {
            if (total > 0.0 && std::abs((perm + other) - total) > 1.0) {
                ValidationIssue issue;
                issue.groupId = groupId;
                issue.fieldKey = "emp_total_total";
                issue.message = QString("employee headcount mismatch: permanent (%1) + other (%2) != total (%3)")
                    .arg(perm).arg(other).arg(total);
                issue.severity = "WARNING";
                issues.append(issue);
            }
        }
    }

    // Employee gender breakdowns
    checkHeadcountGenderSum(groupId, data, "emp_perm_male_no", "emp_perm_female_no", "emp_perm_total", "permanent employee", issues);
    checkHeadcountGenderSum(groupId, data, "emp_other_male_no", "emp_other_female_no", "emp_other_total", "other employee", issues);
    checkHeadcountGenderSum(groupId, data, "emp_total_male_no", "emp_total_female_no", "emp_total_total", "total employee", issues);

    // Board diversity bounds
    if (data.contains("board_female_no") && data.contains("board_total")) {
        double female = 0.0;
        double total = 0.0;
        if (tryParseNumber(data["board_female_no"], female) && tryParseNumber(data["board_total"], total)) {
            if (female > total) {
                ValidationIssue issue;
                issue.groupId = groupId;
                issue.fieldKey = "board_female_no";
                issue.message = QString("board female directors count (%1) exceeds total board size (%2)").arg(female).arg(total);
                issue.severity = "WARNING";
                issues.append(issue);
            }
        }
    }

    // KMP diversity bounds
    if (data.contains("kmp_female_no") && data.contains("kmp_total")) {
        double female = 0.0;
        double total = 0.0;
        if (tryParseNumber(data["kmp_female_no"], female) && tryParseNumber(data["kmp_total"], total)) {
            if (female > total) {
                ValidationIssue issue;
                issue.groupId = groupId;
                issue.fieldKey = "kmp_female_no";
                issue.message = QString("female KMP count (%1) exceeds total KMP count (%2)").arg(female).arg(total);
                issue.severity = "WARNING";
                issues.append(issue);
            }
        }
    }

    // Worker percentage sum
    if (data.contains("wrk_total_male_pct") && data.contains("wrk_total_female_pct")) {
        double malePct = 0.0;
        double femalePct = 0.0;
        if (tryParseNumber(data["wrk_total_male_pct"], malePct) && tryParseNumber(data["wrk_total_female_pct"], femalePct)) {
            double total = malePct + femalePct;
            if (total < 95.0 || total > 105.0) {
                ValidationIssue issue;
                issue.groupId = groupId;
                issue.fieldKey = "wrk_total_male_pct";
                issue.message = QString("worker gender breakdown sum (%1% + %2% = %3%) deviates from 100%")
                    .arg(malePct).arg(femalePct).arg(total);
                issue.severity = "WARNING";
                issues.append(issue);
            }
        }
    }

    // Worker permanent + other vs total
    if (data.contains("wrk_perm_total") && data.contains("wrk_other_total") && data.contains("wrk_total_total")) {
        double perm = 0.0;
        double other = 0.0;
        double total = 0.0;
        if (tryParseNumber(data["wrk_perm_total"], perm) && tryParseNumber(data["wrk_other_total"], other) && tryParseNumber(data["wrk_total_total"], total)) {
            if (total > 0.0 && std::abs((perm + other) - total) > 1.0) {
                ValidationIssue issue;
                issue.groupId = groupId;
                issue.fieldKey = "wrk_total_total";
                issue.message = QString("worker headcount mismatch: permanent (%1) + other (%2) != total (%3)")
                    .arg(perm).arg(other).arg(total);
                issue.severity = "WARNING";
                issues.append(issue);
            }
        }
    }

    // Worker gender breakdowns
    checkHeadcountGenderSum(groupId, data, "wrk_perm_male_no", "wrk_perm_female_no", "wrk_perm_total", "permanent worker", issues);
    checkHeadcountGenderSum(groupId, data, "wrk_other_male_no", "wrk_other_female_no", "wrk_other_total", "other worker", issues);
    checkHeadcountGenderSum(groupId, data, "wrk_total_male_no", "wrk_total_female_no", "wrk_total_total", "total worker", issues);

    // Differently abled bounds
    if (data.contains("da_emp_total") && data.contains("emp_total_total")) {
        double da = 0.0;
        double total = 0.0;
        if (tryParseNumber(data["da_emp_total"], da) && tryParseNumber(data["emp_total_total"], total)) {
            if (da > total && total > 0.0) {
                ValidationIssue issue;
                issue.groupId = groupId;
                issue.fieldKey = "da_emp_total";
                issue.message = QString("differently abled employees count (%1) exceeds total employees (%2)").arg(da).arg(total);
                issue.severity = "WARNING";
                issues.append(issue);
            }
        }
    }
    if (data.contains("da_wrk_total") && data.contains("wrk_total_total")) {
        double da = 0.0;
        double total = 0.0;
        if (tryParseNumber(data["da_wrk_total"], da) && tryParseNumber(data["wrk_total_total"], total)) {
            if (da > total && total > 0.0) {
                ValidationIssue issue;
                issue.groupId = groupId;
                issue.fieldKey = "da_wrk_total";
                issue.message = QString("differently abled workers count (%1) exceeds total workers (%2)").arg(da).arg(total);
                issue.severity = "WARNING";
                issues.append(issue);
            }
        }
    }
}

void checkOperationsConsistency(const QString& groupId, const nlohmann::json& data, QVector<ValidationIssue>& issues)
{
    // National operational locations
    if (data.contains("national_plants") && data.contains("national_offices") && data.contains("national_total")) {
        double plants = 0.0;
        double offices = 0.0;
        double total = 0.0;
        if (tryParseNumber(data["national_plants"], plants) && tryParseNumber(data["national_offices"], offices) && tryParseNumber(data["national_total"], total)) {
            if (total > 0.0 && std::abs((plants + offices) - total) > 1.0) {
                ValidationIssue issue;
                issue.groupId = groupId;
                issue.fieldKey = "national_total";
                issue.message = QString("national locations mismatch: plants (%1) + offices (%2) != total (%3)")
                    .arg(plants).arg(offices).arg(total);
                issue.severity = "WARNING";
                issues.append(issue);
            }
        }
    }

    // International operational locations
    if (data.contains("international_plants") && data.contains("international_offices") && data.contains("international_total")) {
        double plants = 0.0;
        double offices = 0.0;
        double total = 0.0;
        if (tryParseNumber(data["international_plants"], plants) && tryParseNumber(data["international_offices"], offices) && tryParseNumber(data["international_total"], total)) {
            if (total > 0.0 && std::abs((plants + offices) - total) > 1.0) {
                ValidationIssue issue;
                issue.groupId = groupId;
                issue.fieldKey = "international_total";
                issue.message = QString("international locations mismatch: plants (%1) + offices (%2) != total (%3)")
                    .arg(plants).arg(offices).arg(total);
                issue.severity = "WARNING";
                issues.append(issue);
            }
        }
    }

    // CSR obligation vs spend
    if (data.contains("csr_spent_amount") && data.contains("csr_obligation_amount")) {
        double spent = 0.0;
        double obligation = 0.0;
        double unspent = 0.0;
        tryParseNumber(data.value("csr_unspent_amount", nlohmann::json(0.0)), unspent);
        if (tryParseNumber(data["csr_spent_amount"], spent) && tryParseNumber(data["csr_obligation_amount"], obligation)) {
            if (obligation > 0.0 && (spent + unspent + 1.0) < obligation) {
                ValidationIssue issue;
                issue.groupId = groupId;
                issue.fieldKey = "csr_spent_amount";
                issue.message = QString("CSR spent (%1) + unspent (%2) is below required CSR obligation amount (%3)")
                    .arg(spent).arg(unspent).arg(obligation);
                issue.severity = "WARNING";
                issues.append(issue);
            }
        }
    }
}

void checkComplaintsConsistency(const QString& groupId, const nlohmann::json& data, QVector<ValidationIssue>& issues)
{
    auto checkPendingVsFiled = [&](const char* pendingKey, const char* filedKey, const QString& label) {
        if (data.contains(pendingKey) && data.contains(filedKey)) {
            double pending = 0.0;
            double filed = 0.0;
            if (tryParseNumber(data[pendingKey], pending) && tryParseNumber(data[filedKey], filed)) {
                if (pending > filed) {
                    ValidationIssue issue;
                    issue.groupId = groupId;
                    issue.fieldKey = QString::fromLatin1(pendingKey);
                    issue.message = QString("%1: pending complaints (%2) exceed complaints filed (%3)")
                        .arg(label).arg(pending).arg(filed);
                    issue.severity = "WARNING";
                    issues.append(issue);
                }
            }
        }
    };

    checkPendingVsFiled("p3_wc_pending_current", "p3_wc_filed_current", "working conditions (current FY)");
    checkPendingVsFiled("p3_wc_pending_prev", "p3_wc_filed_prev", "working conditions (previous FY)");
    checkPendingVsFiled("p3_hs_pending_current", "p3_hs_filed_current", "health & safety (current FY)");
    checkPendingVsFiled("p3_hs_pending_prev", "p3_hs_filed_prev", "health & safety (previous FY)");

    if (data.contains("stakeholder_grievances") && data["stakeholder_grievances"].is_array()) {
        for (const auto& row : data["stakeholder_grievances"]) {
            if (row.is_object() && row.contains("received_current") && row.contains("resolved_current")) {
                double received = 0.0;
                double resolved = 0.0;
                if (tryParseNumber(row["received_current"], received) && tryParseNumber(row["resolved_current"], resolved)) {
                    if (resolved > received) {
                        ValidationIssue issue;
                        issue.groupId = groupId;
                        issue.fieldKey = "stakeholder_grievances";
                        issue.message = QString("stakeholder grievances: resolved (%1) exceeds received (%2)").arg(resolved).arg(received);
                        issue.severity = "WARNING";
                        issues.append(issue);
                        break;
                    }
                }
            }
        }
    }
}

} // namespace

QVector<ValidationIssue> ReportValidator::validateIssues(const QVector<ReportSection>& sections)
{
    QVector<ValidationIssue> issues;
    for (const ReportSection& section : sections) {
        if (section.status == "FAILED") {
            ValidationIssue issue;
            issue.groupId = section.groupId;
            issue.fieldKey = "";
            issue.message = "extraction failed, no values available";
            issue.severity = "ERROR";
            issues.append(issue);
            continue;
        }

        try {
            nlohmann::json data = nlohmann::json::parse(section.dataJson.toStdString());
            checkMissingAndRangeIssues(section.groupId, data, issues);
            if (section.groupId == "section_a_employees" || section.groupId == "section_a_workers" ||
                section.groupId == "section_a_workforce" || section.groupId == "employees_workers") {
                checkWorkforceConsistency(section.groupId, data, issues);
            } else if (section.groupId == "section_a_operations") {
                checkOperationsConsistency(section.groupId, data, issues);
                checkComplaintsConsistency(section.groupId, data, issues);
            } else if (section.groupId == "principle_3") {
                checkComplaintsConsistency(section.groupId, data, issues);
            }
        } catch (...) {
            ValidationIssue issue;
            issue.groupId = section.groupId;
            issue.fieldKey = "";
            issue.message = "stored data could not be parsed";
            issue.severity = "ERROR";
            issues.append(issue);
        }
    }
    return issues;
}

QStringList ReportValidator::validate(const QVector<ReportSection>& sections)
{
    QStringList warnings;
    const QVector<ValidationIssue> issues = validateIssues(sections);
    for (const ValidationIssue& issue : issues) {
        warnings.append(QString("%1: %2").arg(issue.groupId, issue.message));
    }
    return warnings;
}
