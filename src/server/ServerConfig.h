#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QString>

#include "constants/CommonConstants.h"

inline quint16 resolvePort()
{
    QString portStr = QProcessEnvironment::systemEnvironment().value("PORT");
    if (portStr.isEmpty()) {
        portStr = QProcessEnvironment::systemEnvironment().value("CARBON_ENGINE_PORT");
    }
    if (!portStr.isEmpty()) {
        bool ok = false;
        quint16 p = portStr.toUShort(&ok);
        if (ok && p > 0) {
            return p;
        }
    }
    return ServerDefaults::DefaultPort;
}

const int kEmbeddingDim = ServerDefaults::DefaultEmbeddingDim;
const quint16 kDefaultPort = resolvePort();

inline QString resolveModelPath(const QString &name, const QString &fallback)
{
    QString envVal = QProcessEnvironment::systemEnvironment().value(name);
    if (!envVal.isEmpty()) {
        return envVal;
    }
    // Check container /app/models or /models
    if (QFile::exists("/app/" + fallback)) {
        return "/app/" + fallback;
    }
    if (QFile::exists(fallback)) {
        return fallback;
    }
    // Check user Documents/CarbonESG/models folder
    QString docDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/CarbonESG/models";
    QString docModelPath = QDir(docDir).filePath(QFileInfo(fallback).fileName());
    if (QFile::exists(docModelPath)) {
        return docModelPath;
    }
    QString appRelative = QDir(QCoreApplication::applicationDirPath()).filePath(fallback);
    if (QFile::exists(appRelative)) {
        return appRelative;
    }
    QString parentRelative = QDir(QCoreApplication::applicationDirPath() + "/../../..").filePath(fallback);
    if (QFile::exists(parentRelative)) {
        return parentRelative;
    }
    return fallback;
}

inline QString resolveDatabasePath()
{
    QString envVal = QProcessEnvironment::systemEnvironment().value("CARBON_ENGINE_DB_PATH");
    if (!envVal.isEmpty()) {
        QFileInfo fi(envVal);
        QDir().mkpath(fi.absolutePath());
        return envVal;
    }
    // Check if running inside container with /app/data
    if (QDir("/app/data").exists() || QDir().mkpath("/app/data")) {
        return "/app/data/ce-database.sqlite";
    }
    QString docDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/CarbonESG/data";
    QDir().mkpath(docDir);
    return docDir + "/ce-database.sqlite";
}

const QString kDatabaseFilePath = resolveDatabasePath();
const QString kGenerationModelPath = resolveModelPath("CARBON_ENGINE_GEN_MODEL", Models::LocalGemmaFile);
const QString kEmbeddingModelPath = resolveModelPath("CARBON_ENGINE_EMBD_MODEL", Models::LocalBgeEmbeddingFile);

#endif // SERVER_CONFIG_H
