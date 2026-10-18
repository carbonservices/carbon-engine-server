#include "ReportExporter.h"

// Qt
#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>

// Templating
#include <inja/inja.hpp>
#include <nlohmann/json.hpp>

namespace {

// Safe recursive merge that does not delete default fields when the extracted value is null,
// and ensures array structures are preserved cleanly without breaking template placeholders.
void safeMerge(nlohmann::json& target, const nlohmann::json& patch)
{
    if (!patch.is_object()) return;
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const std::string& key = it.key();
        const auto& val = it.value();
        if (val.is_null()) {
            // Keep target's safe default placeholder
            continue;
        }
        if (val.is_string()) {
            std::string s = val.get<std::string>();
            if (s.empty() || s == "null" || s == "N/A") {
                target[key] = "<Not provided>";
            } else {
                target[key] = s;
            }
        } else if (val.is_array()) {
            if (!val.empty() || !target.contains(key) || !target[key].is_array()) {
                target[key] = val;
            }
        } else if (val.is_object()) {
            if (target.contains(key) && target[key].is_object()) {
                safeMerge(target[key], val);
            } else {
                target[key] = val;
            }
        } else {
            target[key] = val;
        }
    }
}

double parseNumericValue(const nlohmann::json& val)
{
    if (val.is_number()) {
        return val.get<double>();
    }
    if (val.is_string()) {
        try {
            std::string s = val.get<std::string>();
            std::string clean;
            for (char c : s) {
                if ((c >= '0' && c <= '9') || c == '.' || c == '-') {
                    clean += c;
                }
            }
            if (!clean.empty()) {
                return std::stod(clean);
            }
        } catch (...) {
            return 0.0;
        }
    }
    return 0.0;
}

void enrichArrayNumbers(nlohmann::json& arr, const std::string& currKey = "current_value", const std::string& prevKey = "previous_value")
{
    if (!arr.is_array()) return;
    for (auto& item : arr) {
        if (!item.is_object()) continue;
        item["current_num"] = item.contains(currKey) ? parseNumericValue(item[currKey]) : 0.0;
        item["previous_num"] = item.contains(prevKey) ? parseNumericValue(item[prevKey]) : 0.0;
    }
}

void copyAliasIfMissing(nlohmann::json& item, const std::string& targetKey, const std::string& sourceKey)
{
    if (item.contains(sourceKey) && !item.contains(targetKey)) {
        item[targetKey] = item[sourceKey];
    } else if (item.contains(targetKey) && !item.contains(sourceKey)) {
        item[sourceKey] = item[targetKey];
    }
}

void normalizeTableAliases(nlohmann::json& reportData)
{
    auto applyAliases = [](nlohmann::json& arr, const std::vector<std::pair<std::string, std::string>>& pairs) {
        if (!arr.is_array()) return;
        for (auto& item : arr) {
            if (!item.is_object()) continue;
            for (const auto& [a, b] : pairs) {
                copyAliasIfMissing(item, a, b);
            }
        }
    };

    applyAliases(reportData["subsidiaries"], {
        {"name", "company_name"},
        {"type", "business_activity"},
        {"shares_held_pct", "holding_pct"}
    });

    applyAliases(reportData["csr_projects"], {
        {"location", "state_district"}
    });

    applyAliases(reportData["turnover_rates"], {
        {"total_current", "current_rate"},
        {"total_prev", "prev_rate"}
    });

    applyAliases(reportData["business_activities"], {
        {"main_activity", "description"},
        {"business_activity", "description"},
        {"turnover_percentage", "turnover_pct"}
    });

    applyAliases(reportData["products_sold"], {
        {"name", "product_name"},
        {"turnover_percentage", "turnover_pct"}
    });

    applyAliases(reportData["p1_fines"], {
        {"authority", "agency"},
        {"brief_case", "brief"}
    });

    applyAliases(reportData["p2_lca"], {
        {"product", "product_name"}
    });

    applyAliases(reportData["p3_parental_leave_retention"], {
        {"return_to_work_rate", "return_rate"},
        {"gender", "category"}
    });

    applyAliases(reportData["p7_trade_associations"], {
        {"name", "association_name"}
    });

    applyAliases(reportData["p9_product_recalls"], {
        {"recall_count", "number_recalled"}
    });

    applyAliases(reportData["p6_scope123_emissions"], {
        {"scope_name", "scope"}
    });
}

void precomputeChartMetrics(nlohmann::json& reportData)
{
    normalizeTableAliases(reportData);

    enrichArrayNumbers(reportData["p6_energy"]);
    enrichArrayNumbers(reportData["p6_water_withdrawal"]);
    enrichArrayNumbers(reportData["p6_water_discharge"]);
    enrichArrayNumbers(reportData["p6_waste"]);
    enrichArrayNumbers(reportData["p9_complaints"], "received_current", "received_prev");
    enrichArrayNumbers(reportData["p6_scope123_emissions"]);

    nlohmann::json ghgFiltered = nlohmann::json::array();
    if (reportData.contains("p6_ghg_emissions") && reportData["p6_ghg_emissions"].is_array()) {
        for (auto item : reportData["p6_ghg_emissions"]) {
            if (!item.is_object()) continue;
            QString param = item.contains("parameter") && item["parameter"].is_string()
                ? QString::fromStdString(item["parameter"].get<std::string>())
                : QString();
            if (!param.contains("intensity", Qt::CaseInsensitive)) {
                item["current_num"] = item.contains("current_value") ? parseNumericValue(item["current_value"]) : 0.0;
                item["previous_num"] = item.contains("previous_value") ? parseNumericValue(item["previous_value"]) : 0.0;
                ghgFiltered.push_back(item);
            }
        }
    }
    reportData["chart_ghg_filtered"] = ghgFiltered;

    nlohmann::json safetyFiltered = nlohmann::json::array();
    if (reportData.contains("p3_safety_incidents") && reportData["p3_safety_incidents"].is_array()) {
        for (auto item : reportData["p3_safety_incidents"]) {
            if (!item.is_object()) continue;
            QString metricName = item.contains("metric_name") && item["metric_name"].is_string()
                ? QString::fromStdString(item["metric_name"].get<std::string>())
                : (item.contains("category") && item["category"].is_string()
                    ? QString::fromStdString(item["category"].get<std::string>())
                    : QString());
            if (!metricName.contains("fatalities", Qt::CaseInsensitive)) {
                item["current_num"] = item.contains("current_value")
                    ? parseNumericValue(item["current_value"])
                    : (item.contains("ltifr") ? parseNumericValue(item["ltifr"]) : 0.0);
                item["previous_num"] = item.contains("previous_value")
                    ? parseNumericValue(item["previous_value"])
                    : (item.contains("lost_days") ? parseNumericValue(item["lost_days"]) : 0.0);
                if (!item.contains("metric_name")) {
                    item["metric_name"] = metricName.toStdString();
                }
                safetyFiltered.push_back(item);
            }
        }
    }
    reportData["chart_safety_filtered"] = safetyFiltered;

    reportData["chart_gender_male"] = nlohmann::json::array({
        reportData.contains("emp_total_male_no") ? parseNumericValue(reportData["emp_total_male_no"]) : 0.0,
        reportData.contains("wrk_total_male_no") ? parseNumericValue(reportData["wrk_total_male_no"]) : 0.0
    });
    reportData["chart_gender_female"] = nlohmann::json::array({
        reportData.contains("emp_total_female_no") ? parseNumericValue(reportData["emp_total_female_no"]) : 0.0,
        reportData.contains("wrk_total_female_no") ? parseNumericValue(reportData["wrk_total_female_no"]) : 0.0
    });
    reportData["chart_csr_values"] = nlohmann::json::array({
        reportData.contains("csr_obligation_amount") ? parseNumericValue(reportData["csr_obligation_amount"]) : 0.0,
        reportData.contains("csr_spent_amount") ? parseNumericValue(reportData["csr_spent_amount"]) : 0.0
    });
}

} // namespace

ReportExporter::ReportExporter(Database& db, const BRSRSchemaCatalog& catalog) : _db(db), _catalog(catalog) {}

QString ReportExporter::exportJob(int jobId, bool allowDraft)
{
    if (!allowDraft && !_db.isReportLocked(jobId)) {
        _log.error(QString("Export requested for job %1 before it was accepted").arg(jobId));
        return "<h1>Error: report has not been accepted yet</h1>";
    }

    QString tmplPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/CarbonESG/templates/template.html";
    if (!QFile::exists(tmplPath)) {
        tmplPath = QCoreApplication::applicationDirPath() + "/template.html";
    }
    if (!QFile::exists(tmplPath)) {
        tmplPath = QCoreApplication::applicationDirPath() + "/templates/template.html";
    }
    if (!QFile::exists(tmplPath)) {
        tmplPath = QCoreApplication::applicationDirPath() + "/../share/carbon-esg/templates/template.html";
    }
    if (!QFile::exists(tmplPath)) {
        tmplPath = "src/libs/common/templates/template.html";
    }
    if (!QFile::exists(tmplPath)) {
        tmplPath = "template.html";
    }
    QFile file(tmplPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        _log.error("template.html not found on disk");
        return "<h1>Error: template.html not found on disk</h1>";
    }

    nlohmann::json reportData = _catalog.defaultSchema();
    for (const ReportSection& section : _db.getReportSections(jobId)) {
        if (section.status == "FAILED") {
            continue;
        }
        try {
            nlohmann::json sectionData = nlohmann::json::parse(section.dataJson.toStdString());
            safeMerge(reportData, sectionData);
        } catch (const std::exception& e) {
            _log.error(QString("Job %1 section %2 could not be merged: %3").arg(jobId).arg(section.groupId).arg(e.what()));
        }
    }

    // Ensure all loop/table collections are valid JSON arrays (prevents JS syntax error in chart scripts)
    for (const auto& group : _catalog.groups()) {
        for (const auto& loopKey : group.loopFieldKeys) {
            std::string keyStd = loopKey.toStdString();
            if (!reportData.contains(keyStd) || !reportData[keyStd].is_array()) {
                reportData[keyStd] = nlohmann::json::array();
            }
        }
    }

    precomputeChartMetrics(reportData);

    try {
        std::string tmpl = file.readAll().toStdString();
        inja::Environment env;

        // Variadic range supporting both range(count) and range(start, end)
        env.add_callback("range", [](inja::Arguments& args) {
            int start = 0;
            int end = 0;
            if (args.size() == 1) {
                end = args.at(0)->get<int>();
            } else if (args.size() >= 2) {
                start = args.at(0)->get<int>();
                end = args.at(1)->get<int>();
            }
            nlohmann::json res = nlohmann::json::array();
            for (int i = start; i < end; ++i) {
                res.push_back(i);
            }
            return res;
        });

        // Float parser supporting numbers and formatted numeric strings (with commas/symbols)
        env.add_callback("float", 1, [](inja::Arguments& args) {
            try {
                if (args.at(0)->is_number()) {
                    return args.at(0)->get<double>();
                }
                if (args.at(0)->is_string()) {
                    std::string s = args.at(0)->get<std::string>();
                    std::string clean;
                    for (char c : s) {
                        if ((c >= '0' && c <= '9') || c == '.' || c == '-') clean += c;
                    }
                    if (!clean.empty()) return std::stod(clean);
                }
            } catch (...) {}
            return 0.0;
        });

        std::string renderedHtml = env.render(tmpl, reportData);
        _log.info(QString("Report exported for job %1 (allowDraft: %2)").arg(jobId).arg(allowDraft));
        return QString::fromStdString(renderedHtml);
    } catch (const std::exception& e) {
        _log.error(QString("Template render error for job %1: %2").arg(jobId).arg(e.what()));
        return QString("<h1>Template Rendering Error</h1><p>%1</p>").arg(QString::fromUtf8(e.what()).toHtmlEscaped());
    }
}
