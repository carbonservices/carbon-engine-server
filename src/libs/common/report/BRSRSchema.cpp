#include "BRSRSchema.h"
#include <QJsonArray>
#include <QJsonObject>

namespace {

FieldGroup makeGroup(const QString& id, const QString& label, const QString& section,
                      const QString& ragQuery, const QStringList& dataKeys,
                      const QStringList& narrativeKeys, const QStringList& loopKeys)
{
    FieldGroup group;
    group.id = id;
    group.label = label;
    group.section = section;
    group.ragQuery = ragQuery;
    group.dataFieldKeys = dataKeys;
    group.narrativeFieldKeys = narrativeKeys;
    group.loopFieldKeys = loopKeys;
    return group;
}

// The full set of template.html placeholders, each defaulted to "<Not provided>" (scalars) or an empty
// array (loop collections). This guarantees every template variable resolves to something even
// when a field-group's extraction found nothing for it, or hasn't run yet. Kept as one literal
// blob (rather than reconstructed from the FieldGroup catalog below) so the default coverage is
// exactly the template's real placeholder list, not whatever subset the catalog's authors
// remembered to group.
const char* kDefaultBRSRSchemaJson = R"json({"brsr_core_kpis":[],"business_activities":[],"csr_projects":[],"external_assessment":[],"gap_reasons":[],"material_issues":[],"p1_concentration":[],"p1_fines":[],"p1_training":[],"p2_lca":[],"p2_reclaim":[],"p2_recycled_input":[],"p3_grievance_mechanisms":[],"p3_hs_training":[],"p3_parental_leave_retention":[],"p3_performance_reviews":[],"p3_retirement_benefits":[],"p3_safety_incidents":[],"p3_union_membership":[],"p3_wellbeing":[],"p4_stakeholder_engagement":[],"p5_hr_assessments":[],"p5_hr_complaints":[],"p5_hr_training":[],"p5_minimum_wages":[],"p5_remuneration":[],"p6_air_emissions":[],"p6_eia":[],"p6_energy":[],"p6_ghg_emissions":[],"p6_scope123_emissions":[],"p6_waste":[],"p6_water_discharge":[],"p6_water_withdrawal":[],"p7_anticompetitive_cases":[],"p7_policy_advocacy":[],"p7_trade_associations":[],"p8_csr_project_details":[],"p8_rehabilitation":[],"p8_sia":[],"p9_complaints":[],"p9_product_recalls":[],"penalties":[],"principle_policy_matrix":[],"products_sold":[],"review_by_committee":[],"stakeholder_grievances":[],"subsidiaries":[],"turnover_rates":[],"customer_types":"<Not provided>","debug_mode":"false","report_title":"Business Responsibility & Sustainability Report (BRSR)","scope_1_2_intensity_current":"<Not provided>","p6_pat_scheme_details":"<Not provided>","da_emp_perm_female_pct":"<Not provided>","p8_msme_sourcing_prev":"<Not provided>","p2_adverse_impact_mitigation":"<Not provided>","p3_wc_filed_prev":"<Not provided>","p9_cyber_security_policy":"<Not provided>","p5_hr_in_contracts":"<Not provided>","wrk_other_female_no":"<Not provided>","international_total":"<Not provided>","wrk_perm_male_pct":"<Not provided>","wrk_perm_female_pct":"<Not provided>","director_esg_statement":"<Not provided>","p6_sustainability_projects":"<Not provided>","csr_turnover_amount":"<Not provided>","p2_capex_improvements":"<Not provided>","da_wrk_other_male_pct":"<Not provided>","p6_value_chain_env_assessment_pct":"<Not provided>","p3_transition_assistance_programs":"<Not provided>","p9_service_disruption_mechanism":"<Not provided>","emp_perm_male_no":"<Not provided>","da_emp_perm_female_no":"<Not provided>","esg_assessment_scope":"<Not provided>","da_wrk_perm_female_pct":"<Not provided>","p6_circular_economy_initiatives":"<Not provided>","da_emp_other_male_pct":"<Not provided>","da_emp_perm_total":"<Not provided>","p6_climate_bcp":"<Not provided>","p8_local_sourcing_current":"<Not provided>","p3_corrective_actions":"<Not provided>","da_wrk_perm_male_no":"<Not provided>","p3_wc_pending_current":"<Not provided>","p3_accessibility_details":"<Not provided>","markets_national_states":"<Not provided>","emp_perm_male_pct":"<Not provided>","p8_local_supplier_wellbeing":"<Not provided>","da_emp_other_female_no":"<Not provided>","wrk_total_female_pct":"<Not provided>","p4_consultation_process":"<Not provided>","p4_stakeholder_identification_process":"<Not provided>","p8_msme_sourcing_current":"<Not provided>","p3_hs_pending_prev":"<Not provided>","entity_name":"<Not provided>","contact_email":"<Not provided>","paid_up_capital":"<Not provided>","da_wrk_female_no":"<Not provided>","p9_consumer_feedback_mechanism":"<Not provided>","p6_zld_status":"<Not provided>","p6_value_chain_adverse_impact":"<Not provided>","p5_anti_retaliation_mechanism":"<Not provided>","p1_accounts_payable_days":"<Not provided>","da_wrk_other_total":"<Not provided>","national_total":"<Not provided>","wrk_total_male_pct":"<Not provided>","p9_info_channels":"<Not provided>","csr_unspent_amount":"<Not provided>","p6_business_continuity_plan":"<Not provided>","da_wrk_perm_male_pct":"<Not provided>","assurance_statement_summary":"<Not provided>","p5_focal_point_details":"<Not provided>","p2_rd_improvements":"<Not provided>","da_emp_perm_male_pct":"<Not provided>","sustainability_committee_details":"<Not provided>","emp_total_female_no":"<Not provided>","da_emp_female_pct":"<Not provided>","p3_hs_pending_current":"<Not provided>","p6_certifications_audits":"<Not provided>","da_emp_male_no":"<Not provided>","contact_telephone":"<Not provided>","p2_rd_pct_current":"<Not provided>","contact_person_details":"<Not provided>","da_wrk_other_female_pct":"<Not provided>","da_wrk_male_no":"<Not provided>","p3_safe_workplace_measures":"<Not provided>","da_wrk_perm_female_no":"<Not provided>","p2_epr_status":"<Not provided>","da_emp_perm_male_no":"<Not provided>","p3_equal_opportunity_policy":"<Not provided>","p5_value_chain_hr_assessment":"<Not provided>","da_emp_other_total":"<Not provided>","wrk_perm_total":"<Not provided>","p3_wc_filed_current":"<Not provided>","prior_to_previous_fy":"<Not provided>","p3_hs_filed_prev":"<Not provided>","p3_wc_pending_prev":"<Not provided>","p1_conflict_of_interest_process":"<Not provided>","wrk_total_male_no":"<Not provided>","markets_international_countries":"<Not provided>","p1_value_chain_awareness":"<Not provided>","emp_total_male_no":"<Not provided>","year_of_incorporation":"<Not provided>","previous_renewable_energy_pct":"<Not provided>","entity_cin":"<Not provided>","p4_vulnerable_groups_consultation":"<Not provided>","wrk_perm_female_no":"<Not provided>","esg_assessment_done":"<Not provided>","wrk_perm_male_no":"<Not provided>","p4_consultation_outcomes":"<Not provided>","p3_death_compensation_details":"<Not provided>","p9_additional_product_info":"<Not provided>","emp_other_male_pct":"<Not provided>","p6_water_stress_area_discharge":"<Not provided>","p6_ghg_reduction_projects":"<Not provided>","current_renewable_energy_pct":"<Not provided>","csr_spent_amount":"<Not provided>","website_url":"<Not provided>","da_emp_male_pct":"<Not provided>","p8_local_sourcing_prev":"<Not provided>","csr_applicable":"<Not provided>","p3_value_chain_grievance_mechanism":"<Not provided>","kmp_total":"<Not provided>","p6_water_intensity_turnover_current":"<Not provided>","da_wrk_total":"<Not provided>","emp_total_male_pct":"<Not provided>","da_emp_other_male_no":"<Not provided>","emp_other_female_pct":"<Not provided>","emp_perm_female_pct":"<Not provided>","reporting_boundary":"<Not provided>","p3_value_chain_assessment":"<Not provided>","p5_grievance_mechanism":"<Not provided>","p8_community_grievance_mechanism":"<Not provided>","csr_net_worth":"<Not provided>","da_wrk_perm_total":"<Not provided>","assurance_provider_name":"<Not provided>","emp_perm_total":"<Not provided>","p8_negative_impact_mitigation":"<Not provided>","governance_oversight_statement":"<Not provided>","da_emp_total":"<Not provided>","wrk_other_total":"<Not provided>","emp_total_total":"<Not provided>","p2_capex_pct_current":"<Not provided>","p3_ohs_system_implemented":"<Not provided>","da_wrk_other_female_no":"<Not provided>","emp_other_total":"<Not provided>","emp_other_male_no":"<Not provided>","national_plants":"<Not provided>","esg_assessment_agency":"<Not provided>","board_female_pct":"<Not provided>","kmp_female_no":"<Not provided>","wrk_other_male_pct":"<Not provided>","da_emp_female_no":"<Not provided>","reporting_financial_year":"<Not provided>","p9_corrective_actions":"<Not provided>","board_female_no":"<Not provided>","p9_data_privacy_survey_results":"<Not provided>","international_plants":"<Not provided>","previous_fy":"<Not provided>","emp_other_female_no":"<Not provided>","emp_perm_female_no":"<Not provided>","p5_process_modification":"<Not provided>","p3_hazard_reporting_process":"<Not provided>","highest_authority_details":"<Not provided>","assurance_type":"<Not provided>","p3_value_chain_parental_leave":"<Not provided>","p6_value_chain_corrective_actions":"<Not provided>","p3_hazard_identification_process":"<Not provided>","p6_environmental_compliance":"<Not provided>","corporate_address":"<Not provided>","p9_consumer_education_steps":"<Not provided>","kmp_female_pct":"<Not provided>","p2_sustainable_sourcing_process":"<Not provided>","csr_obligation_amount":"<Not provided>","da_wrk_male_pct":"<Not provided>","wrk_other_female_pct":"<Not provided>","wrk_total_female_no":"<Not provided>","p2_rd_pct_prev":"<Not provided>","registered_address":"<Not provided>","p3_hs_filed_current":"<Not provided>","p3_disability_inclusion_measures":"<Not provided>","stock_exchanges":"<Not provided>","p3_healthcare_access":"<Not provided>","scope_1_2_intensity_prev":"<Not provided>","p9_labelled_products_turnover_pct":"<Not provided>","international_offices":"<Not provided>","da_wrk_female_pct":"<Not provided>","national_offices":"<Not provided>","p2_sustainable_sourcing_pct":"<Not provided>","da_emp_other_female_pct":"<Not provided>","p5_corrective_actions":"<Not provided>","export_contribution_pct":"<Not provided>","board_total":"<Not provided>","p6_low_carbon_transition_plan":"<Not provided>","emp_total_female_pct":"<Not provided>","wrk_total_total":"<Not provided>","sustainability_decision_committee_details":"<Not provided>","p6_water_intensity_turnover_prev":"<Not provided>","wrk_other_male_no":"<Not provided>","current_fy":"<Not provided>","da_wrk_other_male_no":"<Not provided>","p2_capex_pct_prev":"<Not provided>"})json";

} // namespace

BRSRSchemaCatalog::BRSRSchemaCatalog()
{
    _groups = {
        makeGroup("section_a_corporate", "Section A: Corporate Identity & Capital", "A",
            "corporate identity number CIN entity name year of incorporation registered office corporate address email telephone website stock exchanges listing paid up capital reporting financial year",
            {"entity_cin", "entity_name", "year_of_incorporation", "registered_address", "corporate_address",
             "contact_email", "contact_telephone", "website_url", "reporting_financial_year", "stock_exchanges",
             "paid_up_capital", "contact_person_details", "reporting_boundary", "assurance_provider_name",
             "assurance_type", "current_fy", "previous_fy", "prior_to_previous_fy"},
            {"customer_types"},
            {}),

        makeGroup("section_a_operations", "Section A: Operations, Markets & CSR", "A",
            "number of plants offices locations national international markets states countries exports contribution turnover business activities products sold subsidiaries CSR net worth obligation spent penalties",
            {"national_plants", "national_offices", "national_total", "international_plants", "international_offices",
             "international_total", "markets_national_states", "markets_international_countries", "export_contribution_pct",
             "csr_applicable", "csr_turnover_amount", "csr_net_worth", "csr_obligation_amount", "csr_spent_amount", "csr_unspent_amount",
             "esg_assessment_done", "esg_assessment_agency", "esg_assessment_scope"},
            {},
            {"business_activities", "products_sold", "subsidiaries", "csr_projects", "stakeholder_grievances", "penalties", "material_issues"}),

        makeGroup("section_a_employees", "Section A: Employees & Governance Diversity", "A",
            "employees permanent other male female total percentage turnover rates board of directors key managerial personnel women representation",
            {"emp_perm_total", "emp_perm_male_no", "emp_perm_male_pct", "emp_perm_female_no", "emp_perm_female_pct",
             "emp_other_total", "emp_other_male_no", "emp_other_male_pct", "emp_other_female_no", "emp_other_female_pct",
             "emp_total_total", "emp_total_male_no", "emp_total_male_pct", "emp_total_female_no", "emp_total_female_pct",
             "board_total", "board_female_no", "board_female_pct", "kmp_total", "kmp_female_no", "kmp_female_pct"},
            {},
            {"turnover_rates"}),

        makeGroup("section_a_workers", "Section A: Workers & Differently-Abled", "A",
            "workers permanent other male female total differently abled employees and workers representation percentage",
            {"wrk_perm_total", "wrk_perm_male_no", "wrk_perm_male_pct", "wrk_perm_female_no", "wrk_perm_female_pct",
             "wrk_other_total", "wrk_other_male_no", "wrk_other_male_pct", "wrk_other_female_no", "wrk_other_female_pct",
             "wrk_total_total", "wrk_total_male_no", "wrk_total_male_pct", "wrk_total_female_no", "wrk_total_female_pct",
             "da_emp_perm_total", "da_emp_perm_male_no", "da_emp_perm_male_pct", "da_emp_perm_female_no", "da_emp_perm_female_pct",
             "da_emp_other_total", "da_emp_other_male_no", "da_emp_other_male_pct", "da_emp_other_female_no", "da_emp_other_female_pct",
             "da_emp_total", "da_emp_male_no", "da_emp_male_pct", "da_emp_female_no", "da_emp_female_pct",
             "da_wrk_perm_total", "da_wrk_perm_male_no", "da_wrk_perm_male_pct", "da_wrk_perm_female_no", "da_wrk_perm_female_pct",
             "da_wrk_other_total", "da_wrk_other_male_no", "da_wrk_other_male_pct", "da_wrk_other_female_no", "da_wrk_other_female_pct",
             "da_wrk_total", "da_wrk_male_no", "da_wrk_male_pct", "da_wrk_female_no", "da_wrk_female_pct"},
            {},
            {}),

        makeGroup("section_b_governance", "Section B: Governance & Oversight", "B",
            "director statement sustainability ESG highest authority responsible sustainability committee governance oversight review committee external assessment",
            {},
            {"director_esg_statement", "highest_authority_details", "sustainability_committee_details",
             "governance_oversight_statement", "sustainability_decision_committee_details"},
            {"review_by_committee", "external_assessment", "gap_reasons"}),

        makeGroup("section_b_policies", "Section B: Principle Policy Matrix", "B",
            "national voluntary guidelines business responsibility policy covers principle board approved statutory commitments value chain web link",
            {},
            {},
            {"principle_policy_matrix"}),

        makeGroup("principle_1", "Principle 1: Ethics, Transparency & Accountability", "P1",
            "anti-corruption training fines penalties conflict of interest accounts payable days value chain awareness",
            {"p1_accounts_payable_days"},
            {"p1_value_chain_awareness", "p1_conflict_of_interest_process"},
            {"p1_training", "p1_fines", "p1_concentration"}),

        makeGroup("principle_2", "Principle 2: Sustainable Goods & Services", "P2",
            "research and development capital expenditure sustainable technologies sourcing extended producer responsibility reclaimed materials life cycle assessment recycled input",
            {"p2_rd_pct_current", "p2_rd_pct_prev", "p2_capex_pct_current", "p2_capex_pct_prev",
             "p2_sustainable_sourcing_process", "p2_sustainable_sourcing_pct"},
            {"p2_rd_improvements", "p2_capex_improvements", "p2_epr_status", "p2_adverse_impact_mitigation"},
            {"p2_reclaim", "p2_lca", "p2_recycled_input"}),

        makeGroup("principle_3", "Principle 3: Employee Wellbeing & Safety", "P3",
            "occupational health safety system hazard identification reporting workplace accessibility equal opportunity compensation retirement benefits parental leave retention union training corrective actions",
            {"p3_ohs_system_implemented", "p3_wc_filed_current", "p3_wc_pending_current", "p3_wc_filed_prev", "p3_wc_pending_prev",
             "p3_hs_filed_current", "p3_hs_pending_current", "p3_hs_filed_prev", "p3_hs_pending_prev"},
            {"p3_accessibility_details", "p3_equal_opportunity_policy", "p3_hazard_identification_process",
             "p3_hazard_reporting_process", "p3_healthcare_access", "p3_safe_workplace_measures",
             "p3_value_chain_assessment", "p3_corrective_actions", "p3_death_compensation_details",
             "p3_disability_inclusion_measures", "p3_value_chain_parental_leave",
             "p3_value_chain_grievance_mechanism", "p3_transition_assistance_programs"},
            {"p3_wellbeing", "p3_safety_incidents", "p3_retirement_benefits", "p3_parental_leave_retention",
             "p3_grievance_mechanisms", "p3_union_membership", "p3_hs_training", "p3_performance_reviews"}),

        makeGroup("principle_4", "Principle 4: Stakeholder Engagement", "P4",
            "stakeholder identification process consultation vulnerable marginalized groups engagement outcomes",
            {},
            {"p4_stakeholder_identification_process", "p4_consultation_process",
             "p4_vulnerable_groups_consultation", "p4_consultation_outcomes"},
            {"p4_stakeholder_engagement"}),

        makeGroup("principle_5", "Principle 5: Human Rights", "P5",
            "human rights focal point grievance mechanism anti-retaliation minimum wages remuneration training complaints assessments",
            {},
            {"p5_focal_point_details", "p5_grievance_mechanism", "p5_anti_retaliation_mechanism",
             "p5_hr_in_contracts", "p5_corrective_actions", "p5_process_modification", "p5_value_chain_hr_assessment"},
            {"p5_hr_training", "p5_minimum_wages", "p5_remuneration", "p5_hr_complaints", "p5_hr_assessments"}),

        makeGroup("principle_6_emissions", "Principle 6: Energy & GHG Emissions", "P6",
            "energy consumption renewable non-renewable sources greenhouse gas emissions scope 1 scope 2 scope 3 air emissions reduction projects low carbon transition",
            {"current_renewable_energy_pct", "previous_renewable_energy_pct",
             "scope_1_2_intensity_current", "scope_1_2_intensity_prev"},
            {"p6_ghg_reduction_projects", "p6_low_carbon_transition_plan"},
            {"p6_energy", "p6_air_emissions", "p6_ghg_emissions", "p6_scope123_emissions"}),

        makeGroup("principle_6_water_waste", "Principle 6: Water, Waste & Environmental Compliance", "P6",
            "water withdrawal consumption discharge zero liquid discharge ZLD effluent waste generated circular economy environmental compliance EIA certifications audits",
            {"p6_water_intensity_turnover_current", "p6_water_intensity_turnover_prev",
             "p6_value_chain_env_assessment_pct"},
            {"p6_pat_scheme_details", "p6_zld_status", "p6_water_stress_area_discharge",
             "p6_environmental_compliance", "p6_circular_economy_initiatives", "p6_sustainability_projects",
             "p6_business_continuity_plan", "p6_value_chain_adverse_impact", "p6_value_chain_corrective_actions",
             "p6_certifications_audits", "p6_climate_bcp"},
            {"p6_water_withdrawal", "p6_water_discharge", "p6_waste", "p6_eia"}),

        makeGroup("principle_7", "Principle 7: Policy Advocacy", "P7",
            "trade and industry associations anti-competitive conduct public policy advocacy positions",
            {}, {},
            {"p7_trade_associations", "p7_anticompetitive_cases", "p7_policy_advocacy"}),

        makeGroup("principle_8", "Principle 8: Inclusive Growth & Community", "P8",
            "social impact assessment rehabilitation resettlement CSR projects MSME local sourcing community grievance",
            {"p8_msme_sourcing_current", "p8_msme_sourcing_prev", "p8_local_sourcing_current", "p8_local_sourcing_prev"},
            {"p8_community_grievance_mechanism", "p8_negative_impact_mitigation", "p8_local_supplier_wellbeing"},
            {"p8_sia", "p8_rehabilitation", "p8_csr_project_details"}),

        makeGroup("principle_9", "Principle 9: Consumer Responsibility", "P9",
            "consumer complaints product recalls feedback mechanism cyber security data privacy product labelling information",
            {"p9_labelled_products_turnover_pct"},
            {"p9_consumer_feedback_mechanism", "p9_cyber_security_policy", "p9_corrective_actions", "p9_info_channels",
             "p9_consumer_education_steps", "p9_service_disruption_mechanism", "p9_additional_product_info",
             "p9_data_privacy_survey_results"},
            {"p9_complaints", "p9_product_recalls"}),

        makeGroup("brsr_core", "BRSR Core Assurance", "CORE",
            "BRSR core key performance indicators assurance statement independent assurance provider",
            {},
            {"assurance_statement_summary"},
            {"brsr_core_kpis"}),
    };
}

const FieldGroup* BRSRSchemaCatalog::findGroup(const QString& groupId) const
{
    for (const FieldGroup& group : _groups) {
        if (group.id == groupId) {
            return &group;
        }
    }
    return nullptr;
}

QStringList BRSRSchemaCatalog::sectionOrder()
{
    return {"A", "B", "P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8", "P9", "CORE"};
}

nlohmann::json BRSRSchemaCatalog::defaultSchema() const
{
    static const nlohmann::json kSchema = nlohmann::json::parse(kDefaultBRSRSchemaJson);
    return kSchema;
}

const QMap<QString, QString>& BRSRSchemaCatalog::fieldLabels()
{
    static const QMap<QString, QString> kLabels = {
        {"entity_cin", "Corporate Identification Number (CIN)"},
        {"entity_name", "Name of the Company / Entity"},
        {"year_of_incorporation", "Year of Incorporation"},
        {"registered_address", "Registered Office Address"},
        {"corporate_address", "Corporate Office Address"},
        {"contact_email", "E-mail Address"},
        {"contact_telephone", "Telephone / Contact Number"},
        {"website_url", "Website URL"},
        {"reporting_financial_year", "Reporting Financial Year"},
        {"stock_exchanges", "Stock Exchanges Where Listed"},
        {"paid_up_capital", "Paid-up Capital (₹)"},
        {"contact_person_details", "Name & Contact of Reporting Officer"},
        {"reporting_boundary", "Reporting Boundary (Standalone / Consolidated)"},
        {"assurance_provider_name", "Name of Assurance Provider"},
        {"assurance_type", "Type of Assurance (Limited / Reasonable)"},
        {"current_fy", "Current Financial Year"},
        {"previous_fy", "Previous Financial Year"},
        {"prior_to_previous_fy", "Prior to Previous Financial Year"},
        {"customer_types", "Customer Categories / Segments"},
        {"national_plants", "Plants / Manufacturing Units (National)"},
        {"national_offices", "Offices (National)"},
        {"national_total", "Total National Operational Locations"},
        {"international_plants", "Plants / Manufacturing Units (International)"},
        {"international_offices", "Offices (International)"},
        {"international_total", "Total International Operational Locations"},
        {"markets_national_states", "States Served in India"},
        {"markets_international_countries", "Countries Served Globally"},
        {"export_contribution_pct", "Contribution of Exports to Turnover (%)"},
        {"csr_applicable", "CSR Applicability (Section 135)"},
        {"csr_turnover_amount", "Turnover for CSR Calculation (₹)"},
        {"csr_net_worth", "Net Worth for CSR Calculation (₹)"},
        {"csr_obligation_amount", "CSR Obligation Amount (₹)"},
        {"csr_spent_amount", "Total CSR Amount Spent (₹)"},
        {"csr_unspent_amount", "CSR Unspent Amount (₹)"},
        {"esg_assessment_done", "External ESG Assessment Conducted?"},
        {"esg_assessment_agency", "ESG Assessment Agency / Framework"},
        {"esg_assessment_scope", "Scope of External ESG Assessment"},
        {"emp_perm_total", "Permanent Employees - Total"},
        {"emp_perm_male_no", "Permanent Employees - Male (No.)"},
        {"emp_perm_male_pct", "Permanent Employees - Male (%)"},
        {"emp_perm_female_no", "Permanent Employees - Female (No.)"},
        {"emp_perm_female_pct", "Permanent Employees - Female (%)"},
        {"emp_other_total", "Other than Permanent Employees - Total"},
        {"emp_other_male_no", "Other than Permanent - Male (No.)"},
        {"emp_other_male_pct", "Other than Permanent - Male (%)"},
        {"emp_other_female_no", "Other than Permanent - Female (No.)"},
        {"emp_other_female_pct", "Other than Permanent - Female (%)"},
        {"emp_total_total", "Total Employees (No.)"},
        {"emp_total_male_no", "Total Employees - Male (No.)"},
        {"emp_total_male_pct", "Total Employees - Male (%)"},
        {"emp_total_female_no", "Total Employees - Female (No.)"},
        {"emp_total_female_pct", "Total Employees - Female (%)"},
        {"board_total", "Board of Directors - Total"},
        {"board_female_no", "Board of Directors - Female (No.)"},
        {"board_female_pct", "Board of Directors - Female (%)"},
        {"kmp_total", "Key Managerial Personnel (KMP) - Total"},
        {"kmp_female_no", "Key Managerial Personnel - Female (No.)"},
        {"kmp_female_pct", "Key Managerial Personnel - Female (%)"},
        {"wrk_perm_total", "Permanent Workers - Total"},
        {"wrk_perm_male_no", "Permanent Workers - Male (No.)"},
        {"wrk_perm_male_pct", "Permanent Workers - Male (%)"},
        {"wrk_perm_female_no", "Permanent Workers - Female (No.)"},
        {"wrk_perm_female_pct", "Permanent Workers - Female (%)"},
        {"wrk_other_total", "Other than Permanent Workers - Total"},
        {"wrk_other_male_no", "Other Workers - Male (No.)"},
        {"wrk_other_male_pct", "Other Workers - Male (%)"},
        {"wrk_other_female_no", "Other Workers - Female (No.)"},
        {"wrk_other_female_pct", "Other Workers - Female (%)"},
        {"wrk_total_total", "Total Workers (No.)"},
        {"wrk_total_male_no", "Total Workers - Male (No.)"},
        {"wrk_total_male_pct", "Total Workers - Male (%)"},
        {"wrk_total_female_no", "Total Workers - Female (No.)"},
        {"wrk_total_female_pct", "Total Workers - Female (%)"},
        {"da_emp_perm_total", "Differently Abled Permanent Employees - Total"},
        {"da_emp_perm_male_no", "Differently Abled Perm Employees - Male (No.)"},
        {"da_emp_perm_male_pct", "Differently Abled Perm Employees - Male (%)"},
        {"da_emp_perm_female_no", "Differently Abled Perm Employees - Female (No.)"},
        {"da_emp_perm_female_pct", "Differently Abled Perm Employees - Female (%)"},
        {"da_emp_other_total", "Differently Abled Other Employees - Total"},
        {"da_emp_other_male_no", "Differently Abled Other Employees - Male (No.)"},
        {"da_emp_other_male_pct", "Differently Abled Other Employees - Male (%)"},
        {"da_emp_other_female_no", "Differently Abled Other Employees - Female (No.)"},
        {"da_emp_other_female_pct", "Differently Abled Other Employees - Female (%)"},
        {"da_emp_total", "Total Differently Abled Employees"},
        {"da_emp_male_no", "Total Differently Abled Employees - Male (No.)"},
        {"da_emp_male_pct", "Total Differently Abled Employees - Male (%)"},
        {"da_emp_female_no", "Total Differently Abled Employees - Female (No.)"},
        {"da_emp_female_pct", "Total Differently Abled Employees - Female (%)"},
        {"da_wrk_perm_total", "Differently Abled Permanent Workers - Total"},
        {"da_wrk_perm_male_no", "Differently Abled Perm Workers - Male (No.)"},
        {"da_wrk_perm_male_pct", "Differently Abled Perm Workers - Male (%)"},
        {"da_wrk_perm_female_no", "Differently Abled Perm Workers - Female (No.)"},
        {"da_wrk_perm_female_pct", "Differently Abled Perm Workers - Female (%)"},
        {"da_wrk_other_total", "Differently Abled Other Workers - Total"},
        {"da_wrk_other_male_no", "Differently Abled Other Workers - Male (No.)"},
        {"da_wrk_other_male_pct", "Differently Abled Other Workers - Male (%)"},
        {"da_wrk_other_female_no", "Differently Abled Other Workers - Female (No.)"},
        {"da_wrk_other_female_pct", "Differently Abled Other Workers - Female (%)"},
        {"da_wrk_total", "Total Differently Abled Workers"},
        {"da_wrk_male_no", "Total Differently Abled Workers - Male (No.)"},
        {"da_wrk_male_pct", "Total Differently Abled Workers - Male (%)"},
        {"da_wrk_female_no", "Total Differently Abled Workers - Female (No.)"},
        {"da_wrk_female_pct", "Total Differently Abled Workers - Female (%)"},
        {"director_esg_statement", "Director / Executive Statement on ESG Strategy"},
        {"highest_authority_details", "Highest Authority Responsible for BRSR Implementation"},
        {"sustainability_committee_details", "Board Sustainability Committee Mandate & Composition"},
        {"governance_oversight_statement", "Governance Oversight & Review Process"},
        {"sustainability_decision_committee_details", "Decision Making Committee Details"},
        {"p1_accounts_payable_days", "Average Accounts Payable Days"},
        {"p1_value_chain_awareness", "Value Chain Programs on Ethics & Anti-Corruption"},
        {"p1_conflict_of_interest_process", "Conflict of Interest Mechanism for Directors & KMP"},
        {"p2_rd_pct_current", "R&D Expenditure in Current FY (%)"},
        {"p2_rd_pct_prev", "R&D Expenditure in Previous FY (%)"},
        {"p2_capex_pct_current", "Capex for Environmental & Social Upgrades - Current FY (%)"},
        {"p2_capex_pct_prev", "Capex for Environmental & Social Upgrades - Previous FY (%)"},
        {"p2_sustainable_sourcing_process", "Procedures for Sustainable Sourcing"},
        {"p2_sustainable_sourcing_pct", "Total Inputs Sourced Sustainably (%)"},
        {"p2_rd_improvements", "R&D Initiatives for Resource Efficiency"},
        {"p2_capex_improvements", "Capex Investments in Clean Technologies"},
        {"p2_epr_status", "Extended Producer Responsibility (EPR) Plan & Status"},
        {"p2_adverse_impact_mitigation", "Mitigation of Adverse Environmental & Social Impacts"},
        {"p3_ohs_system_implemented", "Occupational Health & Safety (OHS) System Coverage"},
        {"p3_wc_filed_current", "Working Conditions Complaints Filed (Current FY)"},
        {"p3_wc_pending_current", "Working Conditions Complaints Pending (Current FY)"},
        {"p3_wc_filed_prev", "Working Conditions Complaints Filed (Previous FY)"},
        {"p3_wc_pending_prev", "Working Conditions Complaints Pending (Previous FY)"},
        {"p3_hs_filed_current", "Health & Safety Complaints Filed (Current FY)"},
        {"p3_hs_pending_current", "Health & Safety Complaints Pending (Current FY)"},
        {"p3_hs_filed_prev", "Health & Safety Complaints Filed (Previous FY)"},
        {"p3_hs_pending_prev", "Health & Safety Complaints Pending (Previous FY)"},
        {"p3_accessibility_details", "Workplace Accessibility for Differently Abled Persons"},
        {"p3_equal_opportunity_policy", "Equal Opportunity Policy & Inclusivity Framework"},
        {"p3_hazard_identification_process", "Work-Related Hazards Identification & Risk Assessment"},
        {"p3_hazard_reporting_process", "Worker Hazard Reporting & Protection from Retaliation"},
        {"p3_healthcare_access", "Non-Occupational Medical & Healthcare Services Access"},
        {"p3_safe_workplace_measures", "Measures for Safe & Healthy Workplace"},
        {"p3_value_chain_assessment", "Value Chain Assessment on Health & Safety"},
        {"p3_corrective_actions", "Corrective Actions on Health & Safety Audits"},
        {"p3_death_compensation_details", "Statutory Rehabilitation & Compensation for Work Deaths"},
        {"p3_disability_inclusion_measures", "Disability Inclusion & Rehabilitation Programs"},
        {"p3_value_chain_parental_leave", "Parental Leave Support across Value Chain"},
        {"p3_value_chain_grievance_mechanism", "Value Chain Worker Grievance Mechanism"},
        {"p3_transition_assistance_programs", "Transition & Reskilling Programs for Employees"},
        {"p4_stakeholder_identification_process", "Process for Identifying Key Stakeholder Groups"},
        {"p4_consultation_process", "Stakeholder Consultation & Engagement Channels"},
        {"p4_vulnerable_groups_consultation", "Engagement with Vulnerable and Marginalized Stakeholders"},
        {"p4_consultation_outcomes", "Key Issues Raised & Management Action Taken"},
        {"p5_focal_point_details", "Human Rights Focal Point / Committee"},
        {"p5_grievance_mechanism", "Human Rights Grievance Redressal Mechanism"},
        {"p5_anti_retaliation_mechanism", "Whistleblower & Anti-Retaliation Protection"},
        {"p5_hr_in_contracts", "Human Rights Clauses in Business Agreements"},
        {"p5_corrective_actions", "Corrective Actions for Human Rights Grievances"},
        {"p5_process_modification", "Business Process Modifications to Prevent Violations"},
        {"p5_value_chain_hr_assessment", "Human Rights Due Diligence Across Value Chain"},
        {"current_renewable_energy_pct", "Renewable Energy Share - Current FY (%)"},
        {"previous_renewable_energy_pct", "Renewable Energy Share - Previous FY (%)"},
        {"scope_1_2_intensity_current", "Scope 1 & 2 GHG Intensity per Revenue (Current FY)"},
        {"scope_1_2_intensity_prev", "Scope 1 & 2 GHG Intensity per Revenue (Previous FY)"},
        {"p6_ghg_reduction_projects", "GHG Reduction Initiatives & Clean Energy Projects"},
        {"p6_low_carbon_transition_plan", "Low Carbon Transition Plan & Net-Zero Target"},
        {"p6_water_intensity_turnover_current", "Water Intensity per Turnover (Current FY)"},
        {"p6_water_intensity_turnover_prev", "Water Intensity per Turnover (Previous FY)"},
        {"p6_value_chain_env_assessment_pct", "Value Chain Partners Evaluated for Environment (%)"},
        {"p6_pat_scheme_details", "Perform Achieve and Trade (PAT) Energy Saving Target"},
        {"p6_zld_status", "Zero Liquid Discharge (ZLD) Implementation Status"},
        {"p6_water_stress_area_discharge", "Effluent Discharge in Water-Stressed Areas"},
        {"p6_environmental_compliance", "Compliance with Applicable Environmental Laws"},
        {"p6_circular_economy_initiatives", "Circular Economy & Waste Minimization Programs"},
        {"p6_sustainability_projects", "Significant Environmental Capital Projects"},
        {"p6_business_continuity_plan", "Environmental Emergency Preparedness & BCP"},
        {"p6_value_chain_adverse_impact", "Adverse Environmental Impacts Identified in Value Chain"},
        {"p6_value_chain_corrective_actions", "Corrective Action Plan for Value Chain Impacts"},
        {"p6_certifications_audits", "Environmental Management Certifications (ISO 14001, etc.)"},
        {"p6_climate_bcp", "Climate Vulnerability & Resilience Planning"},
        {"p7_trade_associations", "Affiliation with Trade and Industry Chambers"},
        {"p7_anticompetitive_cases", "Anti-Competitive Behavior Complaints & Litigations"},
        {"p7_policy_advocacy", "Public Policy Advocacy & Research Submissions"},
        {"p8_msme_sourcing_current", "Procurement from MSMEs - Current FY (%)"},
        {"p8_msme_sourcing_prev", "Procurement from MSMEs - Previous FY (%)"},
        {"p8_local_sourcing_current", "Sourcing from Within District/Local Area - Current FY (%)"},
        {"p8_local_sourcing_prev", "Sourcing from Within District/Local Area - Previous FY (%)"},
        {"p8_community_grievance_mechanism", "Community Grievance Redressal Mechanism"},
        {"p8_negative_impact_mitigation", "Mitigation of Adverse Social Impacts on Local Communities"},
        {"p8_local_supplier_wellbeing", "Capacity Building & Well-being of Local Small Suppliers"},
        {"p9_labelled_products_turnover_pct", "Products Carrying Voluntary Sustainability Labels (%)"},
        {"p9_consumer_feedback_mechanism", "Consumer Feedback & Customer Satisfaction Framework"},
        {"p9_cyber_security_policy", "Cyber Security & Personal Data Protection Framework"},
        {"p9_corrective_actions", "Corrective Actions on Customer Complaints"},
        {"p9_info_channels", "Product Information Channels (Packaging, QR, Web)"},
        {"p9_consumer_education_steps", "Consumer Awareness on Safe & Sustainable Usage"},
        {"p9_service_disruption_mechanism", "Customer Grievance Channels for Service Disruption"},
        {"p9_additional_product_info", "Additional Voluntary Disclosures on Product Impact"},
        {"p9_data_privacy_survey_results", "Data Privacy Audits & Consumer Survey Results"},
        {"assurance_statement_summary", "Independent Reasonable/Limited Assurance Statement Summary"}
    };
    return kLabels;
}

const QMap<QString, QString>& BRSRSchemaCatalog::tableTitles()
{
    static const QMap<QString, QString> kTitles = {
        {"products_sold", "Products & Services Sold (% of Turnover)"},
        {"business_activities", "Main Business Activities & Turnover Share"},
        {"subsidiaries", "Holding, Subsidiary and Joint Venture Companies"},
        {"csr_projects", "Corporate Social Responsibility (CSR) Projects"},
        {"stakeholder_grievances", "Stakeholder Grievances & Redressal Status"},
        {"penalties", "Fines & Penalties Imposed by Regulators"},
        {"material_issues", "Material ESG Issues & Opportunities"},
        {"turnover_rates", "Employee & Worker Turnover Rates"},
        {"principle_policy_matrix", "Principle-wise Policy Matrix (NGRBC)"},
        {"review_by_committee", "Committee Oversight & Reviews of NGRBC Principles"},
        {"external_assessment", "Independent Assessment of Working of Principles by External Agency"},
        {"gap_reasons", "Reasons for Not Formulating Policies"},
        {"p1_training", "Anti-Corruption & Ethics Training Coverage"},
        {"p1_fines", "Fines / Penalties on Ethics & Anti-Corruption"},
        {"p1_concentration", "Customer / Supplier Exposure & Concentration"},
        {"p2_reclaim", "Reclaimed Packaging & End-of-Life Products"},
        {"p2_lca", "Life Cycle Assessments (LCA) Conducted"},
        {"p2_recycled_input", "Recycled / Reused Input Materials"},
        {"p3_wellbeing", "Health, Accident & Maternity Well-being Coverage"},
        {"p3_safety_incidents", "Occupational Health Incidents (LTIFR)"},
        {"p3_retirement_benefits", "Statutory Retirement Benefits Coverage (PF, Gratuity)"},
        {"p3_parental_leave_retention", "Parental Leave Return & Retention Rates"},
        {"p3_grievance_mechanisms", "Working Conditions Grievance Mechanisms"},
        {"p3_union_membership", "Trade Union Representation"},
        {"p3_hs_training", "Health & Safety Training Programs"},
        {"p3_performance_reviews", "Performance & Career Development Appraisals"},
        {"p4_stakeholder_engagement", "Stakeholder Engagement & Consultation Channels"},
        {"p5_hr_training", "Human Rights Awareness & Training"},
        {"p5_minimum_wages", "Payment of Wages Equal to or Above Minimum Wages"},
        {"p5_remuneration", "Median Remuneration & Wage Parity"},
        {"p5_hr_complaints", "Human Rights Complaints & Grievances"},
        {"p5_hr_assessments", "Human Rights Due Diligence across Plants"},
        {"p6_energy", "Energy Consumption Breakdown (Renewable vs Non-Renewable)"},
        {"p6_air_emissions", "Air Emissions Parameters (NOx, SOx, PM, Others)"},
        {"p6_ghg_emissions", "Greenhouse Gas (GHG) Scope 1 & 2 Emissions"},
        {"p6_scope123_emissions", "GHG Scope 1, 2 and 3 Emissions Inventory"},
        {"p6_water_withdrawal", "Water Withdrawal by Source"},
        {"p6_water_discharge", "Water Discharge & Effluent Destination"},
        {"p6_waste", "Waste Generated, Recycled & Disposed"},
        {"p6_eia", "Environmental Impact Assessments (EIA) Undertaken"},
        {"p7_trade_associations", "Affiliations with Trade & Industry Associations"},
        {"p7_anticompetitive_cases", "Inquiries on Anti-Competitive Conduct"},
        {"p7_policy_advocacy", "Public Policy Advocacy Positions & Submissions"},
        {"p8_sia", "Social Impact Assessments (SIA) of Projects"},
        {"p8_rehabilitation", "Rehabilitation & Resettlement (R&R) Projects"},
        {"p8_csr_project_details", "CSR Project Locations & Expenditure"},
        {"p9_complaints", "Consumer Grievances & Complaints Received"},
        {"p9_product_recalls", "Product Recalls on Safety & Quality Grounds"},
        {"brsr_core_kpis", "BRSR Core Key Performance Indicators"}
    };
    return kTitles;
}

const QMap<QString, QVector<TableColumnDef>>& BRSRSchemaCatalog::tableSchemas()
{
    static const QMap<QString, QVector<TableColumnDef>> kSchemas = {
        {"products_sold", {{"product_name", "Product / Service Name", 3}, {"nic_code", "NIC Code", 1}, {"turnover_pct", "% of Turnover", 1}}},
        {"business_activities", {{"description", "Description of Main Business Activity", 3}, {"turnover_pct", "% of Turnover", 1}}},
        {"subsidiaries", {{"company_name", "Company / Subsidiary Name", 3}, {"business_activity", "Type / Business Activity", 2}, {"holding_pct", "% Shares Held", 1}, {"br_participation", "Participates in BR (Yes/No)", 1}}},
        {"csr_projects", {{"project_name", "CSR Project Name", 3}, {"location", "State & District / Location", 2}, {"amount_spent", "Amount Spent (₹ Cr/Lakhs)", 1}}},
        {"stakeholder_grievances", {{"stakeholder_group", "Stakeholder Group", 2}, {"received_current", "Complaints Received (Current FY)", 1}, {"resolved_current", "Complaints Resolved (Current FY)", 1}}},
        {"penalties", {{"authority", "Regulatory Authority", 2}, {"amount", "Penalty Amount (₹)", 1}, {"brief_case", "Brief Case Description", 3}}},
        {"material_issues", {{"material_issue", "Material ESG Issue", 2}, {"rationale", "Rationale / Impact Area", 3}, {"risk_or_opportunity", "Risk / Opportunity", 1}}},
        {"turnover_rates", {{"category", "Workforce Category", 2}, {"male_current", "Current Male (%)", 1}, {"female_current", "Current Female (%)", 1}, {"total_current", "Current Total (%)", 1}, {"male_prev", "Prev Male (%)", 1}, {"female_prev", "Prev Female (%)", 1}, {"total_prev", "Prev Total (%)", 1}}},
        {"principle_policy_matrix", {{"principle", "National Voluntary Principle", 2}, {"policy_formulated", "Policy Formulated (Yes/No)", 1}, {"board_approved", "Board Approved (Yes/No)", 1}}},
        {"review_by_committee", {{"committee_name", "Board / Executive Committee", 2}, {"subject", "Subject Reviewed", 3}, {"frequency", "Frequency", 1}}},
        {"external_assessment", {{"principle_name", "Principle", 2}, {"assessed", "Independently Assessed (Yes/No)", 1}, {"agency", "Name of Assessing Agency", 3}}},
        {"gap_reasons", {{"principle", "Principle", 1}, {"reason", "Reason for Not Formulating Policy", 4}}},
        {"p1_training", {{"category", "Stakeholder Category", 2}, {"anti_corruption_trained_pct", "Trained on Anti-Corruption (%)", 2}}},
        {"p1_fines", {{"agency", "Regulatory Agency", 2}, {"amount", "Fine Amount (₹)", 1}, {"brief", "Brief Case Description", 3}}},
        {"p1_concentration", {{"metric", "Counterparty Metric", 2}, {"value", "Exposure / Share (%)", 1}}},
        {"p2_reclaim", {{"material", "Material / Packaging Type", 2}, {"reclaimed_pct", "Reclaimed / Collected (%)", 1}}},
        {"p2_lca", {{"product_name", "Product / Service with LCA", 3}, {"turnover_pct", "% of Turnover Contributed", 1}}},
        {"p2_recycled_input", {{"material", "Recycled / Reused Material", 2}, {"recycled_pct_current", "Recycled Content (%)", 1}}},
        {"p3_wellbeing", {{"category", "Category (Employees / Workers)", 2}, {"insurance_pct", "Health & Accident Cover (%)", 1}, {"maternity_pct", "Maternity / Parental Cover (%)", 1}}},
        {"p3_safety_incidents", {{"category", "Category (Employees / Workers)", 2}, {"ltifr", "LTIFR (Lost Time Injury Rate)", 1}, {"lost_days", "Lost Workdays", 1}}},
        {"p3_retirement_benefits", {{"benefit", "Benefit (PF / Gratuity / ESI)", 2}, {"emp_covered_pct", "Employees Covered (%)", 1}, {"wrk_covered_pct", "Workers Covered (%)", 1}}},
        {"p3_parental_leave_retention", {{"category", "Gender Category", 2}, {"return_rate", "Return to Work Rate (%)", 1}, {"retention_rate", "12-Month Retention Rate (%)", 1}}},
        {"p3_grievance_mechanisms", {{"category", "Grievance Category", 2}, {"mechanism_details", "Redressal Mechanism Details", 3}}},
        {"p3_union_membership", {{"category", "Workforce Category", 2}, {"union_members_pct", "Trade Union Members (%)", 1}}},
        {"p3_hs_training", {{"category", "Workforce Category", 2}, {"trained_no", "Number Trained", 1}, {"trained_pct", "Trained Percentage (%)", 1}}},
        {"p3_performance_reviews", {{"category", "Workforce Category", 2}, {"male_pct", "Male Reviewed (%)", 1}, {"female_pct", "Female Reviewed (%)", 1}}},
        {"p4_stakeholder_engagement", {{"stakeholder_group", "Stakeholder Group", 2}, {"consultation_channel", "Engagement Channel", 2}, {"purpose", "Purpose & Topics Raised", 3}}},
        {"p5_hr_training", {{"category", "Workforce Category", 2}, {"employees_trained_pct", "Employees Trained (%)", 1}, {"workers_trained_pct", "Workers Trained (%)", 1}}},
        {"p5_minimum_wages", {{"category", "Workforce Category", 2}, {"equal_or_more_pct", "Equal or > Min Wage (%)", 1}, {"less_than_pct", "Less than Min Wage (%)", 1}}},
        {"p5_remuneration", {{"category", "Level (Board / KMP / Other)", 2}, {"male_median", "Male Median Pay (₹)", 1}, {"female_median", "Female Median Pay (₹)", 1}}},
        {"p5_hr_complaints", {{"category", "Complaint Category", 2}, {"filed_current", "Filed in Current FY", 1}, {"pending_current", "Pending Resolution", 1}}},
        {"p5_hr_assessments", {{"aspect", "Human Rights Aspect Assessed", 2}, {"plants_assessed_pct", "Plants Assessed (%)", 1}}},
        {"p6_energy", {{"parameter", "Energy Source / Parameter", 2}, {"current_value", "Current FY (GJ)", 1}, {"previous_value", "Previous FY (GJ)", 1}}},
        {"p6_air_emissions", {{"parameter", "Air Pollutant (NOx, SOx, PM)", 2}, {"unit", "Unit", 1}, {"current_value", "Current FY", 1}, {"previous_value", "Previous FY", 1}}},
        {"p6_ghg_emissions", {{"parameter", "GHG Scope / Source", 2}, {"current_value", "Current FY (tCO₂e)", 1}, {"previous_value", "Previous FY (tCO₂e)", 1}}},
        {"p6_scope123_emissions", {{"scope", "GHG Scope (Scope 1 / 2 / 3)", 2}, {"current_value", "Current FY (tCO₂e)", 1}, {"previous_value", "Previous FY (tCO₂e)", 1}}},
        {"p6_water_withdrawal", {{"parameter", "Water Source (Surface / Ground / Third Party)", 2}, {"current_value", "Current FY (kL)", 1}, {"previous_value", "Previous FY (kL)", 1}}},
        {"p6_water_discharge", {{"destination", "Discharge Destination / Treatment", 2}, {"current_value", "Current FY (kL)", 1}, {"previous_value", "Previous FY (kL)", 1}}},
        {"p6_waste", {{"category", "Waste Stream (Hazardous / Non-Hazardous)", 2}, {"current_value", "Current FY (MT)", 1}, {"previous_value", "Previous FY (MT)", 1}}},
        {"p6_eia", {{"project_name", "Project Subject to EIA", 2}, {"eia_notification_no", "Notification Number", 2}, {"conducted_by", "Independent Agency", 2}}},
        {"p7_trade_associations", {{"association_name", "Trade / Industry Association", 3}, {"reach", "Scope (National / State)", 1}}},
        {"p7_anticompetitive_cases", {{"agency", "Regulatory Agency (e.g. CCI)", 2}, {"brief_case", "Brief Case Description", 3}}},
        {"p7_policy_advocacy", {{"issue_advocated", "Policy Issue Advocated", 3}, {"method", "Representation Method", 2}}},
        {"p8_sia", {{"project_name", "Project Subject to SIA", 2}, {"summary", "Social Impact Summary", 3}, {"web_link", "Web Link", 2}}},
        {"p8_rehabilitation", {{"project_name", "Project with R&R", 2}, {"pafs_affected", "Project Affected Families (PAFs)", 1}}},
        {"p8_csr_project_details", {{"project_name", "CSR Project", 2}, {"state", "State", 1}, {"district", "District", 1}, {"amount_spent", "Amount Spent (₹ Lakhs)", 1}}},
        {"p9_complaints", {{"category", "Complaint Category", 2}, {"received_current", "Received in Current FY", 1}, {"pending_current", "Pending Resolution", 1}}},
        {"p9_product_recalls", {{"reason", "Reason for Recall", 3}, {"number_recalled", "Units Recalled", 1}}},
        {"brsr_core_kpis", {{"kpi_name", "BRSR Core Indicator", 3}, {"current_value", "Current Assured Value", 2}, {"assurance_provider", "Assurance Provider", 2}}}
    };
    return kSchemas;
}

const QMap<QString, QString>& BRSRSchemaCatalog::sectionLabels()
{
    static const QMap<QString, QString> kSectionLabels = {
        {"A", "Section A"},
        {"B", "Section B"},
        {"P1", "Principle 1"},
        {"P2", "Principle 2"},
        {"P3", "Principle 3"},
        {"P4", "Principle 4"},
        {"P5", "Principle 5"},
        {"P6", "Principle 6"},
        {"P7", "Principle 7"},
        {"P8", "Principle 8"},
        {"P9", "Principle 9"},
        {"CORE", "BRSR Core"}
    };
    return kSectionLabels;
}

QString BRSRSchemaCatalog::loopFieldHint(const QString& key)
{
    static const QMap<QString, QString> kHints = {
        {"business_activities", "JSON array of objects with fields [{\"description\": \"...\", \"turnover_pct\": \"...\"}]"},
        {"products_sold", "JSON array of objects with fields [{\"product_name\": \"...\", \"nic_code\": \"...\", \"turnover_pct\": \"...\"}]"},
        {"subsidiaries", "JSON array of objects with fields [{\"company_name\": \"...\", \"business_activity\": \"...\", \"holding_pct\": \"...\", \"br_participation\": \"...\"}]"},
        {"csr_projects", "JSON array of objects with fields [{\"project_name\": \"...\", \"location\": \"...\", \"amount_spent\": \"...\"}]"},
        {"stakeholder_grievances", "JSON array of objects with fields [{\"stakeholder_group\": \"...\", \"received_current\": \"...\", \"resolved_current\": \"...\"}]"},
        {"penalties", "JSON array of objects with fields [{\"authority\": \"...\", \"amount\": \"...\", \"brief_case\": \"...\"}]"},
        {"material_issues", "JSON array of objects with fields [{\"material_issue\": \"...\", \"rationale\": \"...\", \"risk_or_opportunity\": \"...\"}]"},
        {"turnover_rates", "JSON array of objects with fields [{\"category\": \"...\", \"male_current\": \"...\", \"female_current\": \"...\", \"total_current\": \"...\", \"male_prev\": \"...\", \"female_prev\": \"...\", \"total_prev\": \"...\"}]"},
        {"principle_policy_matrix", "JSON array of objects with fields [{\"principle\": \"...\", \"policy_formulated\": \"...\", \"board_approved\": \"...\"}]"},
        {"review_by_committee", "JSON array of objects with fields [{\"subject\": \"...\", \"frequency\": \"...\", \"committee_name\": \"...\"}]"},
        {"external_assessment", "JSON array of objects with fields [{\"principle_name\": \"...\", \"assessed\": \"...\", \"agency\": \"...\"}]"},
        {"gap_reasons", "JSON array of objects with fields [{\"principle\": \"...\", \"reason\": \"...\"}]"},
        {"p1_training", "JSON array of objects with fields [{\"category\": \"...\", \"anti_corruption_trained_pct\": \"...\"}]"},
        {"p1_fines", "JSON array of objects with fields [{\"agency\": \"...\", \"amount\": \"...\", \"brief\": \"...\"}]"},
        {"p1_concentration", "JSON array of objects with fields [{\"metric\": \"...\", \"value\": \"...\"}]"},
        {"p2_reclaim", "JSON array of objects with fields [{\"material\": \"...\", \"reclaimed_pct\": \"...\"}]"},
        {"p2_lca", "JSON array of objects with fields [{\"product_name\": \"...\", \"turnover_pct\": \"...\"}]"},
        {"p2_recycled_input", "JSON array of objects with fields [{\"material\": \"...\", \"recycled_pct_current\": \"...\"}]"},
        {"p3_wellbeing", "JSON array of objects with fields [{\"category\": \"...\", \"insurance_pct\": \"...\", \"maternity_pct\": \"...\"}]"},
        {"p3_safety_incidents", "JSON array of objects with fields [{\"category\": \"...\", \"ltifr\": \"...\", \"lost_days\": \"...\"}]"},
        {"p3_retirement_benefits", "JSON array of objects with fields [{\"benefit\": \"...\", \"emp_covered_pct\": \"...\", \"wrk_covered_pct\": \"...\"}]"},
        {"p3_parental_leave_retention", "JSON array of objects with fields [{\"category\": \"...\", \"return_rate\": \"...\", \"retention_rate\": \"...\"}]"},
        {"p3_grievance_mechanisms", "JSON array of objects with fields [{\"category\": \"...\", \"mechanism_details\": \"...\"}]"},
        {"p3_union_membership", "JSON array of objects with fields [{\"category\": \"...\", \"union_members_pct\": \"...\"}]"},
        {"p3_hs_training", "JSON array of objects with fields [{\"category\": \"...\", \"trained_no\": \"...\", \"trained_pct\": \"...\"}]"},
        {"p3_performance_reviews", "JSON array of objects with fields [{\"category\": \"...\", \"male_pct\": \"...\", \"female_pct\": \"...\"}]"},
        {"p4_stakeholder_engagement", "JSON array of objects with fields [{\"stakeholder_group\": \"...\", \"consultation_channel\": \"...\", \"purpose\": \"...\"}]"},
        {"p5_hr_training", "JSON array of objects with fields [{\"category\": \"...\", \"employees_trained_pct\": \"...\", \"workers_trained_pct\": \"...\"}]"},
        {"p5_minimum_wages", "JSON array of objects with fields [{\"category\": \"...\", \"equal_or_more_pct\": \"...\", \"less_than_pct\": \"...\"}]"},
        {"p5_remuneration", "JSON array of objects with fields [{\"category\": \"...\", \"male_median\": \"...\", \"female_median\": \"...\"}]"},
        {"p5_hr_complaints", "JSON array of objects with fields [{\"category\": \"...\", \"filed_current\": \"...\", \"pending_current\": \"...\"}]"},
        {"p5_hr_assessments", "JSON array of objects with fields [{\"aspect\": \"...\", \"plants_assessed_pct\": \"...\"}]"},
        {"p6_energy", "JSON array of objects with fields [{\"parameter\": \"...\", \"current_value\": \"...\", \"previous_value\": \"...\"}]"},
        {"p6_air_emissions", "JSON array of objects with fields [{\"parameter\": \"...\", \"unit\": \"...\", \"current_value\": \"...\", \"previous_value\": \"...\"}]"},
        {"p6_ghg_emissions", "JSON array of objects with fields [{\"parameter\": \"...\", \"current_value\": \"...\", \"previous_value\": \"...\"}]"},
        {"p6_scope123_emissions", "JSON array of objects with fields [{\"scope\": \"...\", \"current_value\": \"...\", \"previous_value\": \"...\"}]"},
        {"p6_water_withdrawal", "JSON array of objects with fields [{\"parameter\": \"...\", \"current_value\": \"...\", \"previous_value\": \"...\"}]"},
        {"p6_water_discharge", "JSON array of objects with fields [{\"destination\": \"...\", \"current_value\": \"...\", \"previous_value\": \"...\"}]"},
        {"p6_waste", "JSON array of objects with fields [{\"category\": \"...\", \"current_value\": \"...\", \"previous_value\": \"...\"}]"},
        {"p6_eia", "JSON array of objects with fields [{\"project_name\": \"...\", \"eia_notification_no\": \"...\", \"conducted_by\": \"...\"}]"},
        {"p7_trade_associations", "JSON array of objects with fields [{\"association_name\": \"...\", \"reach\": \"...\"}]"},
        {"p7_anticompetitive_cases", "JSON array of objects with fields [{\"agency\": \"...\", \"brief_case\": \"...\"}]"},
        {"p7_policy_advocacy", "JSON array of objects with fields [{\"issue_advocated\": \"...\", \"method\": \"...\"}]"},
        {"p8_sia", "JSON array of objects with fields [{\"project_name\": \"...\", \"summary\": \"...\", \"web_link\": \"...\"}]"},
        {"p8_rehabilitation", "JSON array of objects with fields [{\"project_name\": \"...\", \"pafs_affected\": \"...\"}]"},
        {"p8_csr_project_details", "JSON array of objects with fields [{\"state\": \"...\", \"district\": \"...\", \"project_name\": \"...\", \"amount_spent\": \"...\"}]"},
        {"p9_complaints", "JSON array of objects with fields [{\"category\": \"...\", \"received_current\": \"...\", \"pending_current\": \"...\"}]"},
        {"p9_product_recalls", "JSON array of objects with fields [{\"reason\": \"...\", \"number_recalled\": \"...\"}]"},
        {"brsr_core_kpis", "JSON array of objects with fields [{\"kpi_name\": \"...\", \"current_value\": \"...\", \"assurance_provider\": \"...\"}]"}
    };
    return kHints.value(key, "a JSON array of objects, one per row found in the context");
}

QJsonObject BRSRSchemaCatalog::schemaMetadata() const
{
    QJsonObject meta;

    QJsonArray secOrder;
    for (const QString& sec : sectionOrder()) {
        secOrder.append(sec);
    }
    meta["section_order"] = secOrder;

    QJsonObject secLabelsObj;
    const auto& sLabels = sectionLabels();
    for (auto it = sLabels.begin(); it != sLabels.end(); ++it) {
        secLabelsObj[it.key()] = it.value();
    }
    meta["section_labels"] = secLabelsObj;

    QJsonArray groupsArr;
    for (const FieldGroup& grp : _groups) {
        QJsonObject gObj;
        gObj["id"] = grp.id;
        gObj["label"] = grp.label;
        gObj["section"] = grp.section;
        gObj["data_keys"] = QJsonArray::fromStringList(grp.dataFieldKeys);
        gObj["narrative_keys"] = QJsonArray::fromStringList(grp.narrativeFieldKeys);
        gObj["loop_keys"] = QJsonArray::fromStringList(grp.loopFieldKeys);
        groupsArr.append(gObj);
    }
    meta["groups"] = groupsArr;

    QJsonObject labelsObj;
    const auto& fLabels = fieldLabels();
    for (auto it = fLabels.begin(); it != fLabels.end(); ++it) {
        labelsObj[it.key()] = it.value();
    }
    meta["field_labels"] = labelsObj;

    QJsonObject titlesObj;
    const auto& tTitles = tableTitles();
    for (auto it = tTitles.begin(); it != tTitles.end(); ++it) {
        titlesObj[it.key()] = it.value();
    }
    meta["table_titles"] = titlesObj;

    QJsonObject schemasObj;
    const auto& tSchemas = tableSchemas();
    for (auto it = tSchemas.begin(); it != tSchemas.end(); ++it) {
        QJsonArray colsArr;
        for (const TableColumnDef& col : it.value()) {
            QJsonObject colObj;
            colObj["key"] = col.key;
            colObj["label"] = col.label;
            colObj["flex"] = col.flex;
            colsArr.append(colObj);
        }
        schemasObj[it.key()] = colsArr;
    }
    meta["table_schemas"] = schemasObj;

    return meta;
}
