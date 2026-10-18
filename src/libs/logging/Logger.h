#pragma once

#include <QString>
#include "LogLevel.h"

// Per-module logger writing to log/<ModuleName>_<yyyy-MM-dd>.log (or
// %TEMP%/carbon-engine/log when the app directory isn't writable). Multiple
// Logger instances for the same module name share one underlying file handle.
class Logger {
public:
    explicit Logger(const QString& moduleName);

    void debug(const QString& message) const;
    void info(const QString& message) const;
    void warn(const QString& message) const;
    void warning(const QString& message) const;
    void error(const QString& message) const;
    void critical(const QString& message) const;

    static void setLogDirectory(const QString& dir);
    static QString logDirectory();

private:
    void log(LogLevel level, const QString& message) const;

    QString _moduleName;
};
