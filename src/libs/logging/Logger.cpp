#include "Logger.h"

#include <iostream>
#include <map>
#include <memory>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

namespace {

QString s_customLogDir;
QMutex s_logDirMutex;

bool testWritableDir(const QString& dirPath) {
    if (dirPath.isEmpty() || !QDir().mkpath(dirPath)) {
        return false;
    }
    QFileInfo probe(dirPath + "/.write-test");
    QFile test(probe.filePath());
    if (test.open(QIODevice::WriteOnly)) {
        test.close();
        test.remove();
        return true;
    }
    return false;
}

QString resolveLogDirectory() {
    QMutexLocker locker(&s_logDirMutex);
    if (!s_customLogDir.isEmpty() && testWritableDir(s_customLogDir)) {
        return s_customLogDir;
    }

    // 0. Check CARBON_ENGINE_LOG_DIR environment variable
    QString envLogDir = qEnvironmentVariable("CARBON_ENGINE_LOG_DIR");
    if (!envLogDir.isEmpty() && testWritableDir(envLogDir)) {
        return envLogDir;
    }

    // 1. Check user configured setting in QSettings
    QSettings settings("Carbon Services", "Carbon ESG");
    QString configured = settings.value("paths/logsDir").toString();
    if (!configured.isEmpty() && testWritableDir(configured)) {
        return configured;
    }

    // 2. Default Documents directory: <Documents>/CarbonESG/logs
    QString docsLocation = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docsLocation.isEmpty()) docsLocation = QDir::homePath() + "/Documents";
    QString docsLogDir = QDir::cleanPath(docsLocation + "/CarbonESG/logs");
    if (testWritableDir(docsLogDir)) {
        return docsLogDir;
    }

    // 3. Fallback: app-relative directory (e.g. dev build)
    const QString appRelative = QFileInfo(QCoreApplication::applicationDirPath() + "/log").absoluteFilePath();
    if (testWritableDir(appRelative)) {
        return appRelative;
    }

    // 4. Last resort: per-app system temp directory
    const QString fallback = QDir::cleanPath(QDir::tempPath() + "/CarbonServices/CarbonEngine/log");
    QDir().mkpath(fallback);
    return fallback;
}

// One append-only file handle per module/date, shared by every Logger
// instance for that module so lines interleave correctly instead of each
// instance clobbering the file.
class FileLogSink {
public:
    explicit FileLogSink(const QString& filePath) : _file(filePath) {
        QDir().mkpath(QFileInfo(filePath).absolutePath());
        _file.open(QIODevice::Append | QIODevice::Text);
        _stream = std::make_unique<QTextStream>(&_file);
    }

    ~FileLogSink() {
        if (_file.isOpen()) {
            _stream->flush();
            _file.close();
        }
    }

    void write(LogLevel level, const QString& moduleName, const QString& message) {
        QMutexLocker locker(&_mutex);
        QString line = QString("%1 [%2] %3: %4\n")
                           .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"))
                           .arg(toString(level))
                           .arg(moduleName)
                           .arg(message);

        if (_file.isOpen()) {
            *_stream << line;
            _stream->flush();
        }

        // Echo to stdout / stderr for container runtimes and Cloud Run / Cloud Logging
        if (level == LogLevel::Error || level == LogLevel::Critical) {
            std::cerr << line.toStdString();
        } else {
            std::cout << line.toStdString();
        }
    }

private:
    QFile _file;
    std::unique_ptr<QTextStream> _stream;
    QMutex _mutex;
};

QMutex& registryMutex() {
    static QMutex mutex;
    return mutex;
}

std::map<QString, std::shared_ptr<FileLogSink>>& registry() {
    static std::map<QString, std::shared_ptr<FileLogSink>> sinks;
    return sinks;
}

std::shared_ptr<FileLogSink> sinkFor(const QString& moduleName) {
    const QString dateStamp = QDate::currentDate().toString("yyyy-MM-dd");
    const QString prefix = moduleName + "_";
    const QString key = prefix + dateStamp;

    QMutexLocker locker(&registryMutex());
    auto& reg = registry();
    auto it = reg.find(key);
    if (it != reg.end()) {
        return it->second;
    }

    // Evict expired daily sinks for this module so old file handles close cleanly
    for (auto iter = reg.begin(); iter != reg.end(); ) {
        if (iter->first.startsWith(prefix)) {
            iter = reg.erase(iter);
        } else {
            ++iter;
        }
    }

    const QString filePath = resolveLogDirectory() + "/" + key + ".log";
    auto sink = std::make_shared<FileLogSink>(filePath);
    reg.emplace(key, sink);
    return sink;
}

} // namespace

Logger::Logger(const QString& moduleName) : _moduleName(moduleName) {}

void Logger::debug(const QString& message) const { log(LogLevel::Debug, message); }
void Logger::info(const QString& message) const { log(LogLevel::Info, message); }
void Logger::warn(const QString& message) const { log(LogLevel::Warning, message); }
void Logger::warning(const QString& message) const { log(LogLevel::Warning, message); }
void Logger::error(const QString& message) const { log(LogLevel::Error, message); }
void Logger::critical(const QString& message) const { log(LogLevel::Critical, message); }

void Logger::log(LogLevel level, const QString& message) const {
    sinkFor(_moduleName)->write(level, _moduleName, message);
}

void Logger::setLogDirectory(const QString& dir) {
    QMutexLocker locker(&s_logDirMutex);
    s_customLogDir = dir;
}

QString Logger::logDirectory() {
    return resolveLogDirectory();
}
