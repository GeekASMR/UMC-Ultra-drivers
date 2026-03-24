/*
 * test_asio_spy.cpp - Spy on official ASIO driver to capture exact timing values
 * Loads the official Behringer ASIO DLL and logs:
 *   - bufferSwitchTimeInfo parameters
 *   - getSamplePosition return values
 *   - getLatencies, getBufferSize, outputReady, future results
 */

#include <windows.h>
#include <stdio.h>
#include <mmsystem.h>
#include "src/asio/asio.h"
#include "src/asio/iasiodrv.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// Official Behringer CLSID
static const CLSID CLSID_Official = 
    { 0x0351302f, 0xb1f1, 0x4a5d, { 0x86, 0x13, 0x78, 0x7f, 0x77, 0xc2, 0x0e, 0xa4 } };

static IASIO* g_asio = nullptr;
static volatile long g_cbCount = 0;
static volatile long g_running = 1;
static LARGE_INTEGER g_perfFreq;
static FILE* g_log = nullptr;

// Callback: bufferSwitch
void ASIO_bufferSwitch(long index, ASIOBool directProcess) {
    long count = InterlockedIncrement(&g_cbCount);
    if (count <= 10 || count % 500 == 0) {
        // Also call getSamplePosition to see what it returns during callback
        long long sPos = 0, tStamp = 0;
        ASIOError err = g_asio->getSamplePosition(&sPos, &tStamp);
        
        LARGE_INTEGER qpc;
        QueryPerformanceCounter(&qpc);
        long long qpcNs = (qpc.QuadPart / g_perfFreq.QuadPart) * 1000000000LL +
                          ((qpc.QuadPart % g_perfFreq.QuadPart) * 1000000000LL) / g_perfFreq.QuadPart;
        long long tgtNs = (long long)timeGetTime() * 1000000LL;
        
        fprintf(g_log, "bufferSwitch[%ld] idx=%ld direct=%d | getSP: err=%ld sPos=%lld tStamp=%lld | QPC_ns=%lld tGT_ns=%lld\n",
                count, index, directProcess, err, sPos, tStamp, qpcNs, tgtNs);
        fflush(g_log);
    }
}

// Callback: bufferSwitchTimeInfo
ASIOTime* ASIO_bufferSwitchTimeInfo(ASIOTime* params, long index, ASIOBool directProcess) {
    long count = InterlockedIncrement(&g_cbCount);
    if (count <= 10 || count % 500 == 0) {
        LARGE_INTEGER qpc;
        QueryPerformanceCounter(&qpc);
        long long qpcNs = (qpc.QuadPart / g_perfFreq.QuadPart) * 1000000000LL +
                          ((qpc.QuadPart % g_perfFreq.QuadPart) * 1000000000LL) / g_perfFreq.QuadPart;
        
        fprintf(g_log, "bufferSwitchTimeInfo[%ld] idx=%ld direct=%d\n", count, index, directProcess);
        if (params) {
            fprintf(g_log, "  speed=%.3f sysTime=%lld sPos=%lld rate=%.0f flags=0x%lx\n",
                    params->timeInfo.speed,
                    params->timeInfo.systemTime,
                    params->timeInfo.samplePosition,
                    params->timeInfo.sampleRate,
                    params->timeInfo.flags);
            fprintf(g_log, "  QPC_ns=%lld timeGetTime_ns=%lld\n", qpcNs, (long long)timeGetTime() * 1000000LL);
        }
        fflush(g_log);
        
        // Also call getSamplePosition
        long long sPos = 0, tStamp = 0;
        ASIOError err = g_asio->getSamplePosition(&sPos, &tStamp);
        fprintf(g_log, "  getSP: err=%ld sPos=%lld tStamp=%lld\n", err, sPos, tStamp);
        fflush(g_log);
    }
    return params;
}

void ASIO_sampleRateChanged(ASIOSampleRate rate) {
    fprintf(g_log, "sampleRateChanged: %.0f\n", rate);
    fflush(g_log);
}

long ASIO_asioMessage(long selector, long value, void* msg, double* opt) {
    fprintf(g_log, "asioMessage: selector=%ld value=%ld\n", selector, value);
    fflush(g_log);
    
    switch (selector) {
        case kAsioSelectorSupported:
            if (value == kAsioSupportsTimeInfo) return 1;
            if (value == kAsioEngineVersion) return 1;
            return 0;
        case kAsioEngineVersion: return 2;
        case kAsioSupportsTimeInfo: return 1;
        default: return 0;
    }
}

int main() {
    g_log = fopen("asio_spy_output.txt", "w");
    if (!g_log) { printf("Cannot open log\n"); return 1; }
    
    QueryPerformanceFrequency(&g_perfFreq);
    CoInitialize(nullptr);
    timeBeginPeriod(1);
    
    printf("=== ASIO Official Driver Spy ===\n");
    fprintf(g_log, "=== ASIO Official Driver Spy ===\n");
    
    // Load official DLL 
    const wchar_t* dllPath = L"D:\\Autigravity\\UMCasio\\official_driver\\named_files\\umc_audioasio_x64.dll";
    HMODULE hDll = LoadLibraryW(dllPath);
    if (!hDll) {
        printf("Failed to load DLL: %lu\n", GetLastError());
        return 1;
    }
    
    typedef HRESULT(STDAPICALLTYPE* FnDllGetClassObject)(REFCLSID, REFIID, void**);
    auto pGetClassObject = (FnDllGetClassObject)GetProcAddress(hDll, "DllGetClassObject");
    if (!pGetClassObject) { printf("No DllGetClassObject\n"); return 1; }
    
    IClassFactory* pFactory = nullptr;
    HRESULT hr = pGetClassObject(CLSID_Official, IID_IClassFactory, (void**)&pFactory);
    if (FAILED(hr)) { printf("GetClassObject failed: 0x%08X\n", hr); return 1; }
    
    hr = pFactory->CreateInstance(nullptr, CLSID_Official, (void**)&g_asio);
    pFactory->Release();
    if (FAILED(hr) || !g_asio) { printf("CreateInstance failed: 0x%08X\n", hr); return 1; }
    
    // Init
    if (!g_asio->init(nullptr)) { printf("init() failed\n"); return 1; }
    
    char name[256];
    g_asio->getDriverName(name);
    printf("Driver: %s v%ld\n", name, g_asio->getDriverVersion());
    fprintf(g_log, "Driver: %s v%ld\n", name, g_asio->getDriverVersion());
    
    // Query capabilities
    long numIn = 0, numOut = 0;
    g_asio->getChannels(&numIn, &numOut);
    fprintf(g_log, "Channels: %ld in, %ld out\n", numIn, numOut);
    
    long minBuf, maxBuf, prefBuf, gran;
    g_asio->getBufferSize(&minBuf, &maxBuf, &prefBuf, &gran);
    fprintf(g_log, "BufferSize: min=%ld max=%ld pref=%ld gran=%ld\n", minBuf, maxBuf, prefBuf, gran);
    
    ASIOSampleRate rate;
    g_asio->getSampleRate(&rate);
    fprintf(g_log, "SampleRate: %.0f\n", rate);
    
    // Test future selectors
    ASIOError futureResult;
    futureResult = g_asio->future(kAsioSupportsTimeInfo, nullptr);
    fprintf(g_log, "future(kAsioSupportsTimeInfo) = %ld (ASE_SUCCESS=0x%X)\n", futureResult, ASE_SUCCESS);
    
    futureResult = g_asio->outputReady();
    fprintf(g_log, "outputReady() = %ld\n", futureResult);
    
    // Test latencies BEFORE createBuffers
    long inLat = 0, outLat = 0;
    ASIOError latErr = g_asio->getLatencies(&inLat, &outLat);
    fprintf(g_log, "getLatencies (before create): err=%ld in=%ld out=%ld\n", latErr, inLat, outLat);
    
    // Create buffers with 2 in + 2 out
    int nCh = 4;
    ASIOBufferInfo bufInfo[4];
    bufInfo[0] = { ASIOTrue,  0, {nullptr, nullptr} };
    bufInfo[1] = { ASIOTrue,  1, {nullptr, nullptr} };
    bufInfo[2] = { ASIOFalse, 0, {nullptr, nullptr} };
    bufInfo[3] = { ASIOFalse, 1, {nullptr, nullptr} };
    
    ASIOCallbacks callbacks = {};
    callbacks.bufferSwitch = ASIO_bufferSwitch;
    callbacks.sampleRateDidChange = ASIO_sampleRateChanged;
    callbacks.asioMessage = ASIO_asioMessage;
    callbacks.bufferSwitchTimeInfo = ASIO_bufferSwitchTimeInfo;
    
    ASIOError err2 = g_asio->createBuffers(bufInfo, nCh, prefBuf, &callbacks);
    fprintf(g_log, "createBuffers(%ld) = %ld\n", prefBuf, err2);
    if (err2 != ASE_OK) { printf("createBuffers failed: %ld\n", err2); return 1; }
    
    // Get latencies after create
    latErr = g_asio->getLatencies(&inLat, &outLat);
    fprintf(g_log, "getLatencies (after create): err=%ld in=%ld out=%ld\n", latErr, inLat, outLat);
    
    // Start
    err2 = g_asio->start();
    fprintf(g_log, "start() = %ld\n", err2);
    printf("Streaming for 5 seconds...\n");
    
    Sleep(5000);
    
    fprintf(g_log, "\nTotal callbacks: %ld in 5 seconds (%.1f Hz)\n", g_cbCount, g_cbCount / 5.0);
    
    g_asio->stop();
    g_asio->disposeBuffers();
    g_asio->Release();
    FreeLibrary(hDll);
    
    timeEndPeriod(1);
    CoUninitialize();
    
    fclose(g_log);
    printf("Done! Results in asio_spy_output.txt\n");
    return 0;
}
