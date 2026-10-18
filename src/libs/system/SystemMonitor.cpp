#include "SystemMonitor.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>

// Helper: convert a FILETIME to a 64-bit integer
static quint64 fileTimeToUll(const FILETIME& ft)
{
    ULARGE_INTEGER uli;
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}
#elif defined(__linux__)
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <fstream>
#include <sstream>
#include <QFile>
#endif

// Qt
#include <QMutexLocker>
#include <QThread>
#include <algorithm>

// ─────────────────────────────────────────────────────────────
SystemMonitor::SystemMonitor(QObject* parent)
    : QObject(parent)
{
    detectGpu();

#ifdef _WIN32
    // Seed baseline process times so first delta isn't 0/0.
    FILETIME dum1, dum2, k, u;
    if (GetProcessTimes(GetCurrentProcess(), &dum1, &dum2, &k, &u)) {
        _prevProcKernel = fileTimeToUll(k);
        _prevProcUser   = fileTimeToUll(u);
    }
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        _prevSysIdle   = fileTimeToUll(idle);
        _prevSysKernel = fileTimeToUll(kernel);
        _prevSysUser   = fileTimeToUll(user);
    }
#endif

    connect(&_timer, &QTimer::timeout, this, &SystemMonitor::sample);
    _timer.start(kSampleIntervalMs);
}

SystemMonitor::~SystemMonitor()
{
    _timer.stop();
#ifdef _WIN32
    if (_pdhQuery) {
        PdhCloseQuery(static_cast<PDH_HQUERY>(_pdhQuery));
        _pdhQuery = nullptr;
        _pdhGpuCounter = nullptr;
    }
    if (_pAdapter3) {
        static_cast<IDXGIAdapter3*>(_pAdapter3)->Release();
        _pAdapter3 = nullptr;
    }
#endif
}

// ─────────────────────────────────────────────────────────────
void SystemMonitor::detectGpu()
{
#ifdef _WIN32
    // Enumerate DXGI adapters; select the discrete/primary GPU with the highest dedicated VRAM.
    IDXGIFactory* factory = nullptr;
    if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory),
                                  reinterpret_cast<void**>(&factory)))) {
        _gpuPresent = false;
        return;
    }

    IDXGIAdapter* adapter = nullptr;
    quint64 maxVram = 0;
    QString bestGpuDesc;
    bool foundGpu = false;
    IDXGIAdapter3* selectedAdapter3 = nullptr;

    for (UINT i = 0; SUCCEEDED(factory->EnumAdapters(i, &adapter)); ++i) {
        if (!adapter) continue;
        DXGI_ADAPTER_DESC desc;
        if (SUCCEEDED(adapter->GetDesc(&desc))) {
            // VendorId 0x1414 is the Microsoft Basic Render Driver — skip it.
            if (desc.VendorId != 0x1414) {
                foundGpu = true;
                quint64 vramMb = static_cast<quint64>(desc.DedicatedVideoMemory / (1024 * 1024));
                if (vramMb >= maxVram) {
                    maxVram = vramMb;
                    bestGpuDesc = QString::fromWCharArray(desc.Description);
                    IDXGIAdapter3* a3 = nullptr;
                    if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&a3)))) {
                        if (selectedAdapter3) {
                            selectedAdapter3->Release();
                        }
                        selectedAdapter3 = a3;
                    }
                }
            }
        }
        adapter->Release();
        adapter = nullptr;
    }
    factory->Release();

    if (_pAdapter3) {
        static_cast<IDXGIAdapter3*>(_pAdapter3)->Release();
        _pAdapter3 = nullptr;
    }

    if (foundGpu) {
        _gpuPresent = true;
        _vramTotalMb = maxVram;
        _pAdapter3 = selectedAdapter3;
        _log.info(QString("GPU detected: %1 (VRAM: %2 MB)").arg(bestGpuDesc).arg(_vramTotalMb));
    } else {
        _gpuPresent = false;
        _vramTotalMb = 0;
        if (selectedAdapter3) {
            selectedAdapter3->Release();
        }
    }
#elif defined(__linux__)
    QFile nv("/proc/driver/nvidia/version");
    if (nv.exists()) {
        _gpuPresent = true;
        _vramTotalMb = 8192;
        _log.info("NVIDIA GPU detected via /proc/driver/nvidia");
    } else {
        _gpuPresent = false;
        _vramTotalMb = 0;
    }
#else
    _gpuPresent = false;
    _vramTotalMb = 0;
#endif

    initPdh();
}

void SystemMonitor::initPdh()
{
#ifdef _WIN32
    if (!_gpuPresent) {
        return;
    }

    PDH_HQUERY hQuery = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &hQuery) == ERROR_SUCCESS) {
        PDH_HCOUNTER hCounter = nullptr;
        if (PdhAddEnglishCounterW(hQuery, L"\\GPU Engine(*)\\Utilization Percentage", 0, &hCounter) == ERROR_SUCCESS) {
            _pdhQuery = hQuery;
            _pdhGpuCounter = hCounter;
            PdhCollectQueryData(hQuery);
        } else {
            PdhCloseQuery(hQuery);
            _log.info("GPU Engine performance counters not available; GPU load will fallback to activity state.");
        }
    }
#endif
}

// ─────────────────────────────────────────────────────────────
void SystemMonitor::sample()
{
    QMutexLocker lock(&_mutex);

#ifdef _WIN32
    // ── Process CPU ───────────────────────────────────────────
    {
        FILETIME c, e, k, u;
        if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
            quint64 curK = fileTimeToUll(k);
            quint64 curU = fileTimeToUll(u);
            quint64 dK   = (curK >= _prevProcKernel) ? (curK - _prevProcKernel) : 0ULL;
            quint64 dU   = (curU >= _prevProcUser)   ? (curU - _prevProcUser)   : 0ULL;
            _prevProcKernel = curK;
            _prevProcUser   = curU;
            // Each 100-ns tick == 1 unit; interval is kSampleIntervalMs ms.
            // Normalize across all logical cores so process CPU usage is 0-100%.
            int numCores = (std::max)(1, QThread::idealThreadCount());
            quint64 intervalTicks = static_cast<quint64>(kSampleIntervalMs) * 10000ULL * static_cast<quint64>(numCores);
            double rawPct = (intervalTicks > 0)
                ? (static_cast<double>(dK + dU) / static_cast<double>(intervalTicks)) * 100.0
                : 0.0;
            _cpuPctProcess = (std::min)(100.0, (std::max)(0.0, rawPct));
        }
    }

    // ── System CPU ────────────────────────────────────────────
    {
        FILETIME idle, kernel, user;
        if (GetSystemTimes(&idle, &kernel, &user)) {
            quint64 curIdle   = fileTimeToUll(idle);
            quint64 curKernel = fileTimeToUll(kernel);
            quint64 curUser   = fileTimeToUll(user);
            quint64 dIdle   = (curIdle   >= _prevSysIdle)   ? (curIdle   - _prevSysIdle)   : 0ULL;
            quint64 dKernel = (curKernel >= _prevSysKernel) ? (curKernel - _prevSysKernel) : 0ULL;
            quint64 dUser   = (curUser   >= _prevSysUser)   ? (curUser   - _prevSysUser)   : 0ULL;
            _prevSysIdle   = curIdle;
            _prevSysKernel = curKernel;
            _prevSysUser   = curUser;
            // Kernel includes idle; total = (kernel - idle) + user.
            // Guard against unsigned underflow if dKernel < dIdle.
            quint64 kernelBusy = (dKernel >= dIdle) ? (dKernel - dIdle) : 0ULL;
            quint64 total = kernelBusy + dUser;
            quint64 denom = kernelBusy + dUser + dIdle;
            double rawSys = (denom > 0)
                ? (static_cast<double>(total) / static_cast<double>(denom)) * 100.0
                : 0.0;
            _cpuPctSystem = (std::min)(100.0, (std::max)(0.0, rawSys));
        }
    }

    // ── Process memory ────────────────────────────────────────
    {
        PROCESS_MEMORY_COUNTERS pmc{};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            _memUsedMbProcess = pmc.WorkingSetSize / (1024ULL * 1024ULL);
        }
    }

    // ── System memory ─────────────────────────────────────────
    {
        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms)) {
            _memTotalMbSystem = ms.ullTotalPhys / (1024ULL * 1024ULL);
            _memUsedMbSystem  = (_memTotalMbSystem > ms.ullAvailPhys / (1024ULL * 1024ULL))
                ? _memTotalMbSystem - ms.ullAvailPhys / (1024ULL * 1024ULL)
                : 0;
        }
    }
#elif defined(__linux__)
    // ── Process CPU & Memory via /proc/self/stat and /proc/self/status ──
    std::ifstream statFile("/proc/self/stat");
    if (statFile.is_open()) {
        std::string line;
        std::getline(statFile, line);
        size_t closeParen = line.rfind(')');
        if (closeParen != std::string::npos && closeParen + 2 < line.size()) {
            std::istringstream rest(line.substr(closeParen + 2));
            char state;
            long ppid, pgrp, session, tty_nr, tpgid;
            unsigned long flags, minflt, cminflt, majflt, cmajflt, utime = 0, stime = 0;
            if (rest >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime) {
                long clk = sysconf(_SC_CLK_TCK);
                if (clk <= 0) clk = 100;
                unsigned long totalProc = utime + stime;
                unsigned long dProc = (_prevProcUser > 0 && totalProc >= _prevProcUser) ? (totalProc - _prevProcUser) : 0;
                _prevProcUser = totalProc;
                int numCores = (std::max)(1, QThread::idealThreadCount());
                double rawPct = (static_cast<double>(dProc) / static_cast<double>(clk * numCores)) * 100.0;
                _cpuPctProcess = (std::min)(100.0, (std::max)(0.0, rawPct));
            }
        }
    }

    std::ifstream statusFile("/proc/self/status");
    if (statusFile.is_open()) {
        std::string line;
        while (std::getline(statusFile, line)) {
            if (line.rfind("VmRSS:", 0) == 0) {
                std::istringstream iss(line.substr(6));
                quint64 rssKb = 0;
                iss >> rssKb;
                _memUsedMbProcess = rssKb / 1024ULL;
                break;
            }
        }
    }

    // ── System CPU via /proc/stat ──
    std::ifstream sysStat("/proc/stat");
    if (sysStat.is_open()) {
        std::string cpuLabel;
        unsigned long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
        if (sysStat >> cpuLabel >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal) {
            unsigned long idleTime = idle + iowait;
            unsigned long nonIdleTime = user + nice + system + irq + softirq + steal;
            unsigned long total = idleTime + nonIdleTime;
            unsigned long dTotal = (_prevSysKernel > 0 && total >= _prevSysKernel) ? (total - _prevSysKernel) : 0;
            unsigned long dIdle = (_prevSysIdle > 0 && idleTime >= _prevSysIdle) ? (idleTime - _prevSysIdle) : 0;
            _prevSysKernel = total;
            _prevSysIdle = idleTime;
            if (dTotal > 0) {
                double rawSys = (static_cast<double>(dTotal - dIdle) / static_cast<double>(dTotal)) * 100.0;
                _cpuPctSystem = (std::min)(100.0, (std::max)(0.0, rawSys));
            }
        }
    }

    // ── System memory via /proc/meminfo ──
    std::ifstream meminfo("/proc/meminfo");
    if (meminfo.is_open()) {
        std::string line;
        quint64 totalKb = 0, availKb = 0;
        while (std::getline(meminfo, line)) {
            if (line.rfind("MemTotal:", 0) == 0) {
                std::istringstream iss(line.substr(9));
                iss >> totalKb;
            } else if (line.rfind("MemAvailable:", 0) == 0) {
                std::istringstream iss(line.substr(13));
                iss >> availKb;
            }
        }
        _memTotalMbSystem = totalKb / 1024ULL;
        _memUsedMbSystem = (_memTotalMbSystem > availKb / 1024ULL) ? (_memTotalMbSystem - availKb / 1024ULL) : 0;
    }
#endif

    sampleGpu();
}

void SystemMonitor::sampleGpu()
{
#ifdef _WIN32
    // 1. Real-time VRAM usage via DXGI
    if (_pAdapter3) {
        auto* adapter3 = static_cast<IDXGIAdapter3*>(_pAdapter3);
        DXGI_QUERY_VIDEO_MEMORY_INFO vramInfo{};
        if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vramInfo))) {
            _vramUsedMb = static_cast<quint64>(vramInfo.CurrentUsage / (1024ULL * 1024ULL));
        }
    }

    // 2. Real-time GPU load via PDH
    if (_pdhQuery && _pdhGpuCounter) {
        auto hQuery = static_cast<PDH_HQUERY>(_pdhQuery);
        auto hCounter = static_cast<PDH_HCOUNTER>(_pdhGpuCounter);

        if (PdhCollectQueryData(hQuery) == ERROR_SUCCESS) {
            DWORD bufferSize = 0;
            DWORD itemCount = 0;
            PDH_STATUS status = PdhGetFormattedCounterArrayW(hCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
            if (status == PDH_MORE_DATA && bufferSize > 0) {
                std::vector<BYTE> buffer(bufferSize);
                auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
                if (PdhGetFormattedCounterArrayW(hCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items) == ERROR_SUCCESS) {
                    double totalLoad = 0.0;
                    for (DWORD i = 0; i < itemCount; ++i) {
                        if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA) {
                            totalLoad += items[i].FmtValue.doubleValue;
                        }
                    }
                    _gpuPct = (std::min)(100.0, (std::max)(0.0, totalLoad));
                }
            }
        }
    } else if (_gpuPresent && _vramTotalMb > 0) {
        if (_phase.load() == EnginePhase::Inferencing) {
            _gpuPct = 85.0;
        } else {
            _gpuPct = 0.0;
        }
    } else {
        _gpuPct = 0.0;
    }
#else
    if (_gpuPresent && _vramTotalMb > 0) {
        if (_phase.load() == EnginePhase::Inferencing) {
            _gpuPct = 85.0;
        } else {
            _gpuPct = 0.0;
        }
    } else {
        _gpuPct = 0.0;
    }
#endif
}

// ─────────────────────────────────────────────────────────────
void SystemMonitor::setEnginePhase(EnginePhase phase)
{
    _phase.store(phase);
    // Note: preserve last measured _tokensPerSec on Idle so UI shows completion throughput
}

EnginePhase SystemMonitor::enginePhase() const
{
    return _phase.load();
}

void SystemMonitor::setTokensPerSec(double tps)
{
    if (tps >= 0.0) {
        _tokensPerSec.store(tps);
    }
}

// ─────────────────────────────────────────────────────────────
double SystemMonitor::cpuPctProcess() const
{
    QMutexLocker lock(&_mutex);
    return _cpuPctProcess;
}

double SystemMonitor::cpuPctSystem() const
{
    QMutexLocker lock(&_mutex);
    return _cpuPctSystem;
}

quint64 SystemMonitor::memUsedMbProcess() const
{
    QMutexLocker lock(&_mutex);
    return _memUsedMbProcess;
}

quint64 SystemMonitor::memUsedMbSystem() const
{
    QMutexLocker lock(&_mutex);
    return _memUsedMbSystem;
}

quint64 SystemMonitor::memTotalMbSystem() const
{
    QMutexLocker lock(&_mutex);
    return _memTotalMbSystem;
}

bool SystemMonitor::gpuPresent() const
{
    return _gpuPresent;
}

quint64 SystemMonitor::vramUsedMb() const
{
    QMutexLocker lock(&_mutex);
    return _vramUsedMb;
}

quint64 SystemMonitor::vramTotalMb() const
{
    return _vramTotalMb;
}

double SystemMonitor::gpuPct() const
{
    QMutexLocker lock(&_mutex);
    return _gpuPct;
}

double SystemMonitor::tokensPerSec() const
{
    return _tokensPerSec.load();
}

// ─────────────────────────────────────────────────────────────
QString SystemMonitor::phaseString(EnginePhase phase)
{
    switch (phase) {
    case EnginePhase::Idle:         return QStringLiteral("Idle");
    case EnginePhase::LoadingModel: return QStringLiteral("Loading model");
    case EnginePhase::Embedding:    return QStringLiteral("Embedding");
    case EnginePhase::Inferencing:  return QStringLiteral("Inferencing");
    case EnginePhase::DbReading:    return QStringLiteral("DB Reading");
    case EnginePhase::DbWriting:    return QStringLiteral("DB Writing");
    }
    return QStringLiteral("Unknown");
}

QJsonObject SystemMonitor::getSystemSnapshot() const
{
    double  cpuProc  = cpuPctProcess();
    double  cpuSys   = cpuPctSystem();
    quint64 memProc  = memUsedMbProcess();
    quint64 memSys   = memUsedMbSystem();
    quint64 memTotal = memTotalMbSystem();
    bool    gpuPres  = gpuPresent();
    quint64 vramUsed = vramUsedMb();
    quint64 vramTot  = vramTotalMb();
    double  gpuLoad  = gpuPct();
    double  tps      = tokensPerSec();

    QJsonObject obj;
    obj["cpu_pct_process"]        = qRound(cpuProc * 10.0) / 10.0;
    obj["cpu_pct_system"]         = qRound(cpuSys  * 10.0) / 10.0;
    obj["memory_used_mb_process"] = static_cast<qint64>(memProc);
    obj["memory_used_mb_system"]  = static_cast<qint64>(memSys);
    obj["memory_total_mb_system"] = static_cast<qint64>(memTotal);
    obj["gpu_present"]            = gpuPres;
    obj["vram_used_mb"]           = static_cast<qint64>(vramUsed);
    obj["vram_total_mb"]          = static_cast<qint64>(vramTot);
    obj["gpu_pct"]                = gpuPres ? QJsonValue(qRound(gpuLoad * 10.0) / 10.0) : QJsonValue(QJsonValue::Null);
    obj["engine_status"]          = phaseString(enginePhase());
    obj["tokens_per_sec"]         = qRound(tps * 10.0) / 10.0;
    return obj;
}
