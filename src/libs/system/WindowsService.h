#pragma once

#include <QString>
#include <functional>

namespace WindowsService {

// Default Service Identification
constexpr const wchar_t* kServiceName = L"CarbonEngine";
constexpr const wchar_t* kServiceDisplayName = L"Carbon Engine Service";
constexpr const wchar_t* kServiceDescription = L"Statutory BRSR ESG inference and report generation engine by Carbon Services.";

// Inspects argc/argv for service verbs:
// --service            : Dispatches execution to SCM as a Windows Service
// --install-service    : Registers the Windows Service with SCM (auto-start)
// --uninstall-service  : Stops and removes the Windows Service from SCM
// --start-service      : Starts the registered Windows Service
// --stop-service       : Stops the active Windows Service
// Returns true if a service verb was handled (so main() should exit immediately).
// Returns false if no service verb matched (proceed with normal interactive execution).
bool handleServiceCommands(int argc, char* argv[], std::function<int(int, char**)> runDaemonFunc);

// Programmatic service management methods
bool installService(QString& outMessage);
bool uninstallService(QString& outMessage);
bool startService(QString& outMessage);
bool stopService(QString& outMessage);
bool isServiceInstalled();
bool isServiceRunning();

} // namespace WindowsService

