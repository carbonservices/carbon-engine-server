#include "WindowsService.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsvc.h>
#include <iostream>
#include <QCoreApplication>
#include <QThread>

namespace {

std::function<int(int, char**)> g_runDaemonFunc;
SERVICE_STATUS_HANDLE g_statusHandle = NULL;
SERVICE_STATUS g_serviceStatus = {};
int g_serviceArgc = 0;
char** g_serviceArgv = nullptr;

void ReportServiceStatus(DWORD currentState, DWORD exitCode, DWORD waitHint)
{
    static DWORD checkPoint = 1;

    g_serviceStatus.dwCurrentState = currentState;
    g_serviceStatus.dwWin32ExitCode = exitCode;
    g_serviceStatus.dwWaitHint = waitHint;

    if (currentState == SERVICE_START_PENDING) {
        g_serviceStatus.dwControlsAccepted = 0;
    } else {
        g_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }

    if ((currentState == SERVICE_RUNNING) || (currentState == SERVICE_STOPPED)) {
        g_serviceStatus.dwCheckPoint = 0;
    } else {
        g_serviceStatus.dwCheckPoint = checkPoint++;
    }

    SetServiceStatus(g_statusHandle, &g_serviceStatus);
}

DWORD WINAPI ServiceCtrlHandler(DWORD ctrl, DWORD /*eventType*/, LPVOID /*eventData*/, LPVOID /*context*/)
{
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        ReportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        QCoreApplication::quit();
        return NO_ERROR;
    default:
        break;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

VOID WINAPI ServiceMain(DWORD /*argc*/, LPWSTR* /*argv*/)
{
    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_serviceStatus.dwServiceSpecificExitCode = 0;

    g_statusHandle = RegisterServiceCtrlHandlerExW(WindowsService::kServiceName, ServiceCtrlHandler, NULL);
    if (!g_statusHandle) {
        return;
    }

    ReportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
    ReportServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);

    if (g_runDaemonFunc) {
        g_runDaemonFunc(g_serviceArgc, g_serviceArgv);
    }

    ReportServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

} // namespace

namespace WindowsService {

bool installService(QString& outMessage)
{
    WCHAR path[MAX_PATH];
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) {
        outMessage = "Failed to determine carbon-engine executable path.";
        return false;
    }

    std::wstring binPath = L"\"" + std::wstring(path) + L"\" --service";

    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        outMessage = QString("OpenSCManager failed with error: %1 (Administrator privileges required)").arg(GetLastError());
        return false;
    }

    SC_HANDLE service = CreateServiceW(
        scm,
        kServiceName,
        kServiceDisplayName,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        binPath.c_str(),
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    );

    if (!service) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        if (err == ERROR_SERVICE_EXISTS) {
            outMessage = "CarbonEngine service is already registered in Windows Service Manager.";
            return true;
        }
        outMessage = QString("CreateService failed with error: %1").arg(err);
        return false;
    }

    SERVICE_DESCRIPTIONW desc;
    desc.lpDescription = const_cast<LPWSTR>(kServiceDescription);
    ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &desc);

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    outMessage = "CarbonEngine Windows Service registered successfully (auto-start).";
    return true;
}

bool uninstallService(QString& outMessage)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        outMessage = QString("OpenSCManager failed with error: %1").arg(GetLastError());
        return false;
    }

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_STOP | DELETE);
    if (!service) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            outMessage = "CarbonEngine service is not installed.";
            return true;
        }
        outMessage = QString("OpenService failed with error: %1").arg(err);
        return false;
    }

    SERVICE_STATUS status;
    ControlService(service, SERVICE_CONTROL_STOP, &status);

    if (!DeleteService(service)) {
        DWORD err = GetLastError();
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        outMessage = QString("DeleteService failed with error: %1").arg(err);
        return false;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    outMessage = "CarbonEngine Windows Service removed successfully.";
    return true;
}

bool startService(QString& outMessage)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) {
        outMessage = QString("OpenSCManager failed with error: %1").arg(GetLastError());
        return false;
    }

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!service) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        outMessage = QString("OpenService failed with error: %1").arg(err);
        return false;
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded = 0;
    if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        if (ssp.dwCurrentState == SERVICE_RUNNING || ssp.dwCurrentState == SERVICE_START_PENDING) {
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            outMessage = "CarbonEngine service is already running.";
            return true;
        }
    }

    if (!StartServiceW(service, 0, NULL)) {
        DWORD err = GetLastError();
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        outMessage = QString("StartService failed with error: %1").arg(err);
        return false;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    outMessage = "CarbonEngine service start signal dispatched successfully.";
    return true;
}

bool stopService(QString& outMessage)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) {
        outMessage = QString("OpenSCManager failed with error: %1").arg(GetLastError());
        return false;
    }

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!service) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        outMessage = QString("OpenService failed with error: %1").arg(err);
        return false;
    }

    SERVICE_STATUS status;
    if (!ControlService(service, SERVICE_CONTROL_STOP, &status)) {
        DWORD err = GetLastError();
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        if (err == ERROR_SERVICE_NOT_ACTIVE) {
            outMessage = "CarbonEngine service is already stopped.";
            return true;
        }
        outMessage = QString("ControlService STOP failed with error: %1").arg(err);
        return false;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    outMessage = "CarbonEngine service stop signal dispatched successfully.";
    return true;
}

bool isServiceInstalled()
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return false;

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_QUERY_STATUS);
    bool installed = (service != NULL);
    if (service) CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return installed;
}

bool isServiceRunning()
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return false;

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_QUERY_STATUS);
    if (!service) {
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded = 0;
    bool running = false;
    if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        running = (ssp.dwCurrentState == SERVICE_RUNNING);
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return running;
}

bool handleServiceCommands(int argc, char* argv[], std::function<int(int, char**)> runDaemonFunc)
{
    if (argc < 2) {
        return false;
    }

    std::string arg = argv[1];

    if (arg == "--service") {
        g_runDaemonFunc = runDaemonFunc;
        g_serviceArgc = argc;
        g_serviceArgv = argv;
        SERVICE_TABLE_ENTRYW dispatchTable[] = {
            { (LPWSTR)kServiceName, (LPSERVICE_MAIN_FUNCTIONW)ServiceMain },
            { NULL, NULL }
        };
        StartServiceCtrlDispatcherW(dispatchTable);
        return true;
    }

    if (arg == "--install-service") {
        QString msg;
        bool ok = installService(msg);
        std::cout << msg.toStdString() << std::endl;
        return true;
    }

    if (arg == "--uninstall-service") {
        QString msg;
        bool ok = uninstallService(msg);
        std::cout << msg.toStdString() << std::endl;
        return true;
    }

    if (arg == "--start-service") {
        QString msg;
        bool ok = startService(msg);
        std::cout << msg.toStdString() << std::endl;
        return true;
    }

    if (arg == "--stop-service") {
        QString msg;
        bool ok = stopService(msg);
        std::cout << msg.toStdString() << std::endl;
        return true;
    }

    return false;
}

} // namespace WindowsService

#else

// Non-Windows POSIX stub implementation
namespace WindowsService {

bool installService(QString& outMessage) { outMessage = "Windows Services not supported on POSIX."; return false; }
bool uninstallService(QString& outMessage) { outMessage = "Windows Services not supported on POSIX."; return false; }
bool startService(QString& outMessage) { outMessage = "Windows Services not supported on POSIX."; return false; }
bool stopService(QString& outMessage) { outMessage = "Windows Services not supported on POSIX."; return false; }
bool isServiceInstalled() { return false; }
bool isServiceRunning() { return false; }
bool handleServiceCommands(int, char*[], std::function<int(int, char**)>) { return false; }

} // namespace WindowsService

#endif

