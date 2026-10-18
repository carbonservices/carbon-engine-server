#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <nlohmann/json.hpp>

struct TableColumnDef {
    QString key;
    QString label;
    int flex{1};
};

// One addressable cluster of related template fields: a single RAG query plus a single
// LLM extraction call populate exactly the fields listed here. Keeping each group's field
// count small is what makes per-group extraction reliable on a 7B CPU-only model, unlike
// asking for the whole ~300-field template in one call.
struct FieldGroup {
    QString id;
    QString label;
    QString section;
    QString ragQuery;
    QStringList dataFieldKeys;
    QStringList narrativeFieldKeys;
    QStringList loopFieldKeys;

    QStringList allFieldKeys() const
    {
        QStringList keys = dataFieldKeys;
        keys.append(narrativeFieldKeys);
        keys.append(loopFieldKeys);
        return keys;
    }
};

// Owns the full inventory of field groups that make up template.html, and the default
// (all-"N/A"/empty-array) schema that guarantees every template placeholder resolves to
// something even when a group's extraction found nothing.
class BRSRSchemaCatalog {
public:
    BRSRSchemaCatalog();

    const QVector<FieldGroup>& groups() const { return _groups; }
    const FieldGroup* findGroup(const QString& groupId) const;

    // Every section id a group can belong to, in template/display order.
    static QStringList sectionOrder();
    static const QMap<QString, QString>& sectionLabels();
    static QString loopFieldHint(const QString& key);

    nlohmann::json defaultSchema() const;

    static const QMap<QString, QString>& fieldLabels();
    static const QMap<QString, QString>& tableTitles();
    static const QMap<QString, QVector<TableColumnDef>>& tableSchemas();

    QJsonObject schemaMetadata() const;

private:
    QVector<FieldGroup> _groups;
};

