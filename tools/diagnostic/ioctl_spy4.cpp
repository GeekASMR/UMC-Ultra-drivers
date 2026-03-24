/*
 * ioctl_spy4.cpp - 完整播放过程 IOCTL 追踪
 *
 * 目标：在官方 DLL 实际播放正弦波期间，抓取：
 *   1. 完整 IOCTL 序列（重点 ENABLE_STREAM + WAIT_FOR_BUFFER）
 *   2. DMA 输出缓冲区内容（看是否被清零）
 *   3. ctrlPage 的变化
 *   4. bufferSwitch 回调时的缓冲区状态
 *
 * 编译: cl /EHsc /O2 ioctl_spy4.cpp /link ole32.lib oleaut32.lib dbghelp.lib
 */
#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

static FILE* g_log = NULL;
static LARGE_INTEGER g_startTick, g_perfFreq;

static double elapsed_ms() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - g_startTick.QuadPart) * 1000.0 / g_perfFreq.QuadPart;
}

// =========================================================================
// Hook types
// =========================================================================
typedef BOOL (WINAPI *PFN_DIO)(HANDLE,DWORD,LPVOID,DWORD,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
typedef HANDLE (WINAPI *PFN_CFW)(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
typedef HANDLE (WINAPI *PFN_CEW)(LPSECURITY_ATTRIBUTES,BOOL,BOOL,LPCWSTR);

static PFN_DIO g_oDIO = NULL;
static PFN_CFW g_oCFW = NULL;
static PFN_CEW g_oCEW = NULL;
static int g_evtCount = 0;

// IOCTL name table
static const char* ioctl_name(DWORD code) {
    switch(code) {
        case 0x80882004: return "GET_DEVICE_INFO";
        case 0x808820C4: return "GET_DEVICE_PROPS";
        case 0x80882804: return "SET_MODE";
        case 0x80882808: return "GET_STREAM_CONFIG";
        case 0x8088280C: return "GET_CHANNEL_LIST";
        case 0x80882810: return "GET_CHANNEL_INFO";
        case 0x80882820: return "GET_STATUS";
        case 0x80882824: return "SET_BUFFER_SIZE";
        case 0x80882840: return "SELECT_CHANNEL";
        case 0x80882844: return "DESELECT_CHANNEL";
        case 0x80882880: return "SET_CALLBACKS";
        case 0x808828A0: return "MAP_CHANNEL_BUFFER";
        case 0x808828C0: return "ENABLE_STREAM";
        case 0x808828C4: return "STOP_STREAMING";
        case 0x808828C8: return "START_STREAMING";
        case 0x808828F4: return "WAIT_FOR_BUFFER";
        default: return "UNKNOWN";
    }
}

// Counters for WAIT_FOR_BUFFER (don't spam the log)
static volatile LONG g_wfbCount = 0;
static volatile LONG g_wfbOkCount = 0;
static volatile LONG g_wfbPendingCount = 0;

static void hexdump(FILE* f, const void* d, DWORD sz, int max) {
    const unsigned char* p = (const unsigned char*)d;
    DWORD show = sz < (DWORD)max ? sz : (DWORD)max;
    for (DWORD i = 0; i < show; i++) {
        fprintf(f, "%02X ", p[i]);
        if ((i+1)%32==0) fprintf(f, "\n      ");
    }
    if (show < sz) fprintf(f, "...(+%u)", sz-show);
    fprintf(f, "\n");
}

// =========================================================================
// Hook: DeviceIoControl
// =========================================================================
BOOL WINAPI Hook_DIO(HANDLE hDev, DWORD code, LPVOID in, DWORD inSz,
    LPVOID out, DWORD outSz, LPDWORD ret, LPOVERLAPPED ov) {

    BOOL ok = g_oDIO(hDev, code, in, inSz, out, outSz, ret, ov);
    DWORD err = ok ? 0 : GetLastError();
    DWORD devType = (code >> 16) & 0xFFFF;

    if (devType == 0x8088 && g_log) {
        DWORD func = (code >> 2) & 0xFFF;
        DWORD br = ret ? *ret : 0;
        const char* name = ioctl_name(code);

        // For WAIT_FOR_BUFFER, only count — don't spam the log
        if (code == 0x808828F4) {
            InterlockedIncrement(&g_wfbCount);
            if (ok) InterlockedIncrement(&g_wfbOkCount);
            else if (err == ERROR_IO_PENDING) InterlockedIncrement(&g_wfbPendingCount);
        } else {
            // Log all other IOCTLs with full detail
            fprintf(g_log, "\n[IO] +%.1fms  0x%08X  %-20s  %s(err=%lu,ret=%u)\n",
                    elapsed_ms(), code, name, ok?"OK":"FAIL", err, br);
            if (in && inSz > 0) {
                fprintf(g_log, "  In(%u): ", inSz);
                hexdump(g_log, in, inSz, 128);
            }
            if (ok && out && br > 0) {
                fprintf(g_log, "  Out(%u): ", br);
                hexdump(g_log, out, br, 128);
            }

            // Special: log MAP_CHANNEL_BUFFER input in detail
            if (code == 0x808828A0 && in && inSz >= 24) {
                BYTE* p = (BYTE*)in;
                fprintf(g_log, "  MAP detail: chId=0x%02X type=0x%04X bufBytes=%u bps=%u dmaAddr=%p\n",
                        *(DWORD*)&p[0], *(DWORD*)&p[4], *(DWORD*)&p[8],
                        *(DWORD*)&p[12], (void*)*(UINT64*)&p[16]);
            }

            // Special: log ENABLE_STREAM input
            if (code == 0x808828C0 && in && inSz >= 4) {
                fprintf(g_log, "  ENABLE detail: bufferSize=%u\n", *(DWORD*)in);
            }

            // Special: log SET_CALLBACKS
            if (code == 0x80882880 && in && inSz >= 32) {
                BYTE* p = (BYTE*)in;
                fprintf(g_log, "  CALLBACKS: ctrlPage=%p evt0=%p evt1=%p evt2=%p\n",
                        (void*)*(UINT64*)&p[0], (void*)*(UINT64*)&p[8],
                        (void*)*(UINT64*)&p[16], (void*)*(UINT64*)&p[24]);
            }

            fflush(g_log);
        }
    }
    if (!ok) SetLastError(err);
    return ok;
}

HANDLE WINAPI Hook_CEW(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL init, LPCWSTR name) {
    HANDLE h = g_oCEW(sa, manual, init, name);
    g_evtCount++;
    fprintf(g_log, "[EVT] +%.1fms CreateEvent(%s,%s) = %p (#%d)\n",
            elapsed_ms(), manual?"MAN":"AUTO", init?"SET":"UNSET", h, g_evtCount);
    fflush(g_log);
    return h;
}

HANDLE WINAPI Hook_CFW(LPCWSTR fn, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t) {
    HANDLE h = g_oCFW(fn, a, s, sa, c, f, t);
    if (fn && (wcsstr(fn,L"vid_1397")||wcsstr(fn,L"tusbaudio")||wcsstr(fn,L"215a80ef")))
        fprintf(g_log, "\n[DEV] +%.1fms CreateFileW: %ls => %p (flags=0x%X)\n", elapsed_ms(), fn, h, f);
    fflush(g_log);
    return h;
}

// =========================================================================
// IAT Patch helper
// =========================================================================
static bool PatchIAT(HMODULE hMod, const char* dll, const char* func, void* nf, void** of) {
    ULONG sz; auto im = (PIMAGE_IMPORT_DESCRIPTOR)ImageDirectoryEntryToDataEx(hMod,TRUE,IMAGE_DIRECTORY_ENTRY_IMPORT,&sz,NULL);
    if (!im) return false;
    for (; im->Name; im++) {
        if (_stricmp((char*)((BYTE*)hMod+im->Name), dll) != 0) continue;
        auto ot = (PIMAGE_THUNK_DATA)((BYTE*)hMod+im->OriginalFirstThunk);
        auto th = (PIMAGE_THUNK_DATA)((BYTE*)hMod+im->FirstThunk);
        for (; ot->u1.AddressOfData; ot++, th++) {
            if (ot->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            auto imp = (PIMAGE_IMPORT_BY_NAME)((BYTE*)hMod+ot->u1.AddressOfData);
            if (strcmp(imp->Name, func)==0) {
                DWORD op; VirtualProtect(&th->u1.Function, 8, PAGE_READWRITE, &op);
                *of = (void*)th->u1.Function;
                th->u1.Function = (ULONG_PTR)nf;
                VirtualProtect(&th->u1.Function, 8, op, &op);
                return true;
            }
        }
    }
    return false;
}

// =========================================================================
// main - 加载官方 DLL，播放正弦波，抓取完整 IOCTL trace
// =========================================================================
int main() {
    printf("=== Spy v4 - Complete playback IOCTL trace ===\n");
    printf("Goal: Capture ENABLE_STREAM + WAIT_FOR_BUFFER + DMA buffer state\n\n");

    QueryPerformanceFrequency(&g_perfFreq);
    QueryPerformanceCounter(&g_startTick);

    g_log = fopen("D:\\Autigravity\\UMCasio\\ioctl_trace4.log", "w");
    if (!g_log) { printf("Cannot open log file\n"); return 1; }
    fprintf(g_log, "=== TUSBAUDIO Trace v4 (Complete Playback) ===\n");
    fprintf(g_log, "=== %s ===\n\n", "Captures ENABLE_STREAM, WAIT_FOR_BUFFER, and DMA buffer state");

    // Load official DLL
    HMODULE hDll = LoadLibraryW(L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioasio_x64.dll");
    if (!hDll) {
        // Try alternate path
        hDll = LoadLibraryW(L"C:\\Program Files\\Behringer\\UMC_Audio_Driver\\x64\\umc_audioasio_x64.dll");
    }
    if (!hDll) { printf("Failed to load official DLL\n"); return 1; }
    printf("DLL loaded at %p\n", hDll);

    // Hook IATs
    const char* dlls[] = {"kernel32.dll", "api-ms-win-core-io-l1-1-0.dll",
        "api-ms-win-core-file-l1-1-0.dll", "api-ms-win-core-synch-l1-2-0.dll",
        "api-ms-win-core-synch-l1-1-0.dll", "api-ms-win-core-file-l1-2-0.dll"};
    for (auto d : dlls) {
        if (!g_oDIO) PatchIAT(hDll, d, "DeviceIoControl", (void*)Hook_DIO, (void**)&g_oDIO);
        if (!g_oCFW) PatchIAT(hDll, d, "CreateFileW", (void*)Hook_CFW, (void**)&g_oCFW);
        if (!g_oCEW) PatchIAT(hDll, d, "CreateEventW", (void*)Hook_CEW, (void**)&g_oCEW);
    }
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (!g_oDIO) g_oDIO = (PFN_DIO)GetProcAddress(k32, "DeviceIoControl");
    if (!g_oCFW) g_oCFW = (PFN_CFW)GetProcAddress(k32, "CreateFileW");
    if (!g_oCEW) g_oCEW = (PFN_CEW)GetProcAddress(k32, "CreateEventW");

    printf("Hooks installed: DIO=%d CFW=%d CEW=%d\n", !!g_oDIO, !!g_oCFW, !!g_oCEW);

    // Create ASIO instance via COM
    typedef HRESULT (WINAPI *FnGCO)(REFCLSID, REFIID, LPVOID*);
    auto pGCO = (FnGCO)GetProcAddress(hDll, "DllGetClassObject");
    if (!pGCO) { printf("DllGetClassObject not found\n"); return 1; }

    const CLSID clsid = {0x0351302f,0xb1f1,0x4a5d,{0x86,0x13,0x78,0x7f,0x77,0xc2,0x0e,0xa4}};
    IClassFactory* pF = nullptr;
    pGCO(clsid, IID_IClassFactory, (void**)&pF);
    IUnknown* pU = nullptr;
    pF->CreateInstance(NULL, clsid, (void**)&pU);
    pF->Release();
    void** vt = *(void***)pU;

    // ASIO vtable function types
    typedef BOOL (__thiscall *fn_init)(void*, void*);
    typedef void (__thiscall *fn_getDriverName)(void*, char*);
    typedef long (__thiscall *fn_getChannels)(void*, long*, long*);
    typedef long (__thiscall *fn_getBufferSize)(void*, long*, long*, long*, long*);
    typedef long (__thiscall *fn_getSampleRate)(void*, double*);
    typedef long (__thiscall *fn_getChannelInfo)(void*, void*);
    typedef long (__thiscall *fn_outputReady)(void*);

    #pragma pack(push, 4)
    struct ASIOBufferInfo { long isInput; long channelNum; void* buffers[2]; };
    struct ASIOCallbacks {
        void (*bufferSwitch)(long, long);
        void (*sampleRateDidChange)(double);
        long (*asioMessage)(long, long, void*, double*);
        void* (*bufferSwitchTimeInfo)(void*, long, long);
    };
    #pragma pack(pop)

    typedef long (__thiscall *fn_createBuffers)(void*, ASIOBufferInfo*, long, long, ASIOCallbacks*);
    typedef long (__thiscall *fn_start)(void*);
    typedef long (__thiscall *fn_stop)(void*);
    typedef long (__thiscall *fn_disposeBuffers)(void*);

    // ====================================================================
    // Step 1: init
    // ====================================================================
    fprintf(g_log, "\n========== PHASE 1: INIT ==========\n");
    printf("\n--- init() ---\n");
    BOOL initOk = ((fn_init)vt[3])(pU, NULL);
    printf("init: %d\n", initOk);
    if (!initOk) { printf("INIT FAILED\n"); return 1; }

    long nIn, nOut;
    ((fn_getChannels)vt[9])(pU, &nIn, &nOut);
    printf("Channels: %ld in / %ld out\n", nIn, nOut);
    fprintf(g_log, "\nChannels: %ld in / %ld out\n", nIn, nOut);

    double sampleRate = 0;
    ((fn_getSampleRate)vt[13])(pU, &sampleRate);
    printf("Sample rate: %.0f Hz\n", sampleRate);
    fprintf(g_log, "Sample rate: %.0f Hz\n", sampleRate);

    long minB, maxB, prefB, gran;
    ((fn_getBufferSize)vt[11])(pU, &minB, &maxB, &prefB, &gran);
    printf("Buffer: min=%ld max=%ld pref=%ld gran=%ld\n", minB, maxB, prefB, gran);
    fprintf(g_log, "Buffer: min=%ld max=%ld pref=%ld gran=%ld\n", minB, maxB, prefB, gran);

    // ====================================================================
    // Step 2: createBuffers (In 1 + Out 1, using preferred size)
    // ====================================================================
    fprintf(g_log, "\n========== PHASE 2: CREATE BUFFERS ==========\n");
    printf("\n--- createBuffers(%ld) ---\n", prefB);

    // bufferSwitch state
    static volatile long g_switchCount = 0;
    static volatile long g_lastBufIdx = -1;
    static ASIOBufferInfo* g_pBI = nullptr;
    static long g_bufSize = 0;

    struct Callbacks {
        static void bufferSwitch(long idx, long directProcess) {
            InterlockedIncrement(&g_switchCount);
            g_lastBufIdx = idx;

            // Write sine wave into output buffer during callback!
            if (g_pBI && g_bufSize > 0) {
                // g_pBI[1] is output channel
                void* outBuf = g_pBI[1].buffers[idx];
                if (outBuf) {
                    // Write 1kHz sine @ -6dB as 32-bit INT (24-bit left-aligned)
                    long* samples = (long*)outBuf;
                    static double phase = 0.0;
                    double freq = 1000.0;
                    double sr = 48000.0;
                    double amp = 0.5 * 8388607.0; // -6dB * 24-bit max
                    for (long i = 0; i < g_bufSize; i++) {
                        double val = amp * sin(phase);
                        samples[i] = ((long)val) << 8; // 24-bit left-aligned in 32-bit
                        phase += 2.0 * 3.14159265358979 * freq / sr;
                        if (phase > 2.0 * 3.14159265358979) phase -= 2.0 * 3.14159265358979;
                    }
                }
            }
        }
        static void sampleRateDidChange(double r) {}
        static long asioMessage(long sel, long val, void* msg, double* opt) {
            if (sel == 1 || sel == 7) return 1; // kAsioSelectorSupported, kAsioSupportsTimeInfo
            return 0;
        }
    };

    ASIOCallbacks cbs = { Callbacks::bufferSwitch, Callbacks::sampleRateDidChange,
                          Callbacks::asioMessage, NULL };

    ASIOBufferInfo bi[2] = {};
    bi[0].isInput = 1; bi[0].channelNum = 0; bi[0].buffers[0] = 0; bi[0].buffers[1] = 0;
    bi[1].isInput = 0; bi[1].channelNum = 0; bi[1].buffers[0] = 0; bi[1].buffers[1] = 0;

    g_pBI = bi;
    g_bufSize = prefB;

    long cbr = ((fn_createBuffers)vt[19])(pU, bi, 2, prefB, &cbs);
    printf("createBuffers: %ld (0=OK)\n", cbr);
    fprintf(g_log, "\ncreateBuffers result: %ld\n", cbr);

    if (cbr != 0) { printf("CREATE BUFFERS FAILED\n"); return 1; }

    printf("In  buf[0]=%p buf[1]=%p\n", bi[0].buffers[0], bi[0].buffers[1]);
    printf("Out buf[0]=%p buf[1]=%p\n", bi[1].buffers[0], bi[1].buffers[1]);
    fprintf(g_log, "In  buf[0]=%p buf[1]=%p\n", bi[0].buffers[0], bi[0].buffers[1]);
    fprintf(g_log, "Out buf[0]=%p buf[1]=%p\n", bi[1].buffers[0], bi[1].buffers[1]);

    // Tell official DLL we're ready for output
    long orr = ((fn_outputReady)vt[22])(pU);
    printf("outputReady: %ld\n", orr);
    fprintf(g_log, "outputReady: %ld\n", orr);

    // ====================================================================
    // Step 3: start — this is where ENABLE_STREAM should appear!
    // ====================================================================
    fprintf(g_log, "\n========== PHASE 3: START (watch for ENABLE_STREAM!) ==========\n");
    printf("\n--- start() ---\n");

    long sr = ((fn_start)vt[7])(pU);
    printf("start: %ld (0=OK)\n", sr);
    fprintf(g_log, "\nstart result: %ld\n", sr);

    if (sr != 0) { printf("START FAILED\n"); return 1; }

    // ====================================================================
    // Step 4: Let it run! Monitor everything for 3 seconds
    // ====================================================================
    fprintf(g_log, "\n========== PHASE 4: PLAYING (3 seconds) ==========\n");
    printf("\n--- Playing for 3 seconds... ---\n");
    printf("  Writing 1kHz sine into output buffer in each bufferSwitch callback\n\n");

    for (int sec = 0; sec < 6; sec++) {
        Sleep(500);
        long sc = InterlockedExchange(&g_switchCount, g_switchCount);
        long wfb = InterlockedExchange(&g_wfbCount, g_wfbCount);

        printf("  +%.1fs: bufferSwitch=%ld  WFB_calls=%ld (ok=%ld pend=%ld)  lastIdx=%ld\n",
               (sec+1)*0.5, sc, wfb, g_wfbOkCount, g_wfbPendingCount, (long)g_lastBufIdx);

        // Dump output buffer content (check if our sine data survived or was zeroed)
        if (bi[1].buffers[0]) {
            long* outSamples = (long*)bi[1].buffers[g_lastBufIdx >= 0 ? g_lastBufIdx : 0];
            int nonZero = 0;
            for (long i = 0; i < prefB && i < 128; i++) {
                if (outSamples[i] != 0) nonZero++;
            }
            printf("           Out buf[%ld]: nonZero=%d/%ld  first4=[%08X %08X %08X %08X]\n",
                   g_lastBufIdx, nonZero, prefB < 128 ? prefB : 128L,
                   outSamples[0], outSamples[1], outSamples[2], outSamples[3]);
        }

        // Dump input buffer content
        if (bi[0].buffers[0]) {
            long* inSamples = (long*)bi[0].buffers[g_lastBufIdx >= 0 ? g_lastBufIdx : 0];
            int nonZero = 0;
            for (long i = 0; i < prefB && i < 128; i++) {
                if (inSamples[i] != 0) nonZero++;
            }
            printf("           In  buf[%ld]: nonZero=%d/%ld\n",
                   g_lastBufIdx, nonZero, prefB < 128 ? prefB : 128L);
        }

        // Log periodic summary
        fprintf(g_log, "\n[STATUS] +%.1fs: switchCount=%ld WFB=%ld(ok=%ld,pend=%ld)\n",
                (sec+1)*0.5, sc, wfb, (long)g_wfbOkCount, (long)g_wfbPendingCount);
    }

    // Final WFB stats
    fprintf(g_log, "\n========== WAIT_FOR_BUFFER STATS ==========\n");
    fprintf(g_log, "Total WFB calls: %ld\n", (long)g_wfbCount);
    fprintf(g_log, "WFB OK (immediate): %ld\n", (long)g_wfbOkCount);
    fprintf(g_log, "WFB PENDING: %ld\n", (long)g_wfbPendingCount);
    fprintf(g_log, "Total bufferSwitch callbacks: %ld\n", (long)g_switchCount);

    // ====================================================================
    // Step 5: Stop
    // ====================================================================
    fprintf(g_log, "\n========== PHASE 5: STOP ==========\n");
    printf("\n--- stop() ---\n");
    ((fn_stop)vt[8])(pU);

    fprintf(g_log, "\n========== PHASE 6: DISPOSE ==========\n");
    printf("--- disposeBuffers() ---\n");
    ((fn_disposeBuffers)vt[20])(pU);

    // Cleanup
    pU->Release();

    fprintf(g_log, "\n========== SUMMARY ==========\n");
    fprintf(g_log, "Events created: %d\n", g_evtCount);
    fprintf(g_log, "bufferSwitch count: %ld\n", (long)g_switchCount);
    fprintf(g_log, "WAIT_FOR_BUFFER total: %ld\n", (long)g_wfbCount);
    fprintf(g_log, "\nDone.\n");
    fclose(g_log);

    printf("\n=== Done! See ioctl_trace4.log ===\n");
    printf("Key things to look for:\n");
    printf("  1. Did ENABLE_STREAM (0x808828C0) appear? With what params?\n");
    printf("  2. How many WAIT_FOR_BUFFER calls? Pattern?\n");
    printf("  3. Was output buffer non-zero (sine data survived)?\n");
    return 0;
}
