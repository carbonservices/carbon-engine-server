#pragma once

#include <QString>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <QLockFile>
#include <QStandardPaths>
#include <memory>
#endif

// Machine-wide single-instance guard to prevent duplicate process instances.
// On Windows, uses a Global\ Named Mutex across all sessions including Session 0 (Windows Services).
// On Linux/POSIX, uses QLockFile in standard temporary runtime directory.
class SingleInstanceGuard {
public:
    explicit SingleInstanceGuard(const QString& name)
    {
#ifdef _WIN32
        std::wstring wideName = (L"Global\\" + name).toStdWString();
        _mutex = CreateMutexW(NULL, TRUE, wideName.c_str());
        DWORD error = GetLastError();

        if (_mutex != NULL && error == ERROR_ALREADY_EXISTS) {
            _isPrimary = false;
            CloseHandle(_mutex);
            _mutex = NULL;
        } else if (_mutex != NULL && error == ERROR_SUCCESS) {
            _isPrimary = true;
        } else {
            // Fallback to Local\ if Global\ creation failed (e.g. non-elevated user token)
            std::wstring localName = (L"Local\\" + name).toStdWString();
            _mutex = CreateMutexW(NULL, TRUE, localName.c_str());
            _isPrimary = (_mutex != NULL && GetLastError() != ERROR_ALREADY_EXISTS);
        }
#else
        QString lockPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/" + name + ".lock";
        _lockFile = std::make_unique<QLockFile>(lockPath);
        _isPrimary = _lockFile->tryLock(100);
#endif
    }

    ~SingleInstanceGuard()
    {
#ifdef _WIN32
        if (_mutex) {
            ReleaseMutex(_mutex);
            CloseHandle(_mutex);
            _mutex = NULL;
        }
#else
        if (_lockFile && _isPrimary) {
            _lockFile->unlock();
        }
#endif
    }

    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

    bool isPrimary() const { return _isPrimary; }

private:
    bool _isPrimary{false};
#ifdef _WIN32
    HANDLE _mutex{NULL};
#else
    std::unique_ptr<QLockFile> _lockFile;
#endif
};

