/*
 * ioctl_spy5.cpp - 改进版播放追踪 (v5)
 *
 * v4 的问题: bufferSwitch 回调未触发 (count=0)
 * 
 * v5 改进:
 *   1. 追踪 ctrlPage 内容 (看看驱动是否在推进 counter)
 *   2. 监控 SET_CALLBACKS 传入的 3 个事件的触发情况
 *   3. 增加写入 DMA 缓冲区的直接方式（不依赖 bufferSwitch）
 *   4. 更长的等待时间 (5秒)
 *   5. 追踪 ENABLE_STREAM 的存在/缺失
 *
 * 编译: cl /EHsc /O2 ioctl_spy5.cpp /link ole32.lib oleaut32.lib dbghelp.lib
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

// Captured state from IOCTLs
static volatile BYTE* g_capturedCtrlPage = NULL;
static HANDLE g_capturedEvents[3] = {NULL, NULL, NULL};
static volatile BYTE* g_outDmaAddr = NULL;
static volatile BYTE* g_inDmaAddr = NULL;
static long g_outBufBytes = 0;
static long g_inBufBytes = 0;

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
static volatile LONG g_enableStreamCalled = 0;

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

        // For WAIT_FOR_BUFFER, count
        if (code == 0x808828F4) {
            InterlockedIncrement(&g_wfbCount);
            if (ok) InterlockedIncrement(&g_wfbOkCount);
            else if (err == ERROR_IO_PENDING) InterlockedIncrement(&g_wfbPendingCount);
            // Log first few WFB calls
            if (g_wfbCount <= 5) {
                fprintf(g_log, "\n[IO] +%.1fms  0x%08X  %-20s  %s(err=%lu,ret=%u) [WFB#%ld]\n",
                        elapsed_ms(), code, name, ok?"OK":"FAIL", err, br, (long)g_wfbCount);
                if (in && inSz > 0) {
                    fprintf(g_log, "  In(%u): ", inSz);
                    hexdump(g_log, in, inSz, 64);
                }
                if (ok && out && br > 0) {
                    fprintf(g_log, "  Out(%u): ", br);
                    hexdump(g_log, out, br, 64);
                }
                fflush(g_log);
            }
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
                DWORD chId = *(DWORD*)&p[0];
                DWORD type = *(DWORD*)&p[4];
                DWORD bufB = *(DWORD*)&p[8];
                DWORD bps = *(DWORD*)&p[12];
                UINT64 dmaAddr = *(UINT64*)&p[16];
                fprintf(g_log, "  MAP detail: chId=0x%02X type=0x%04X bufBytes=%u bps=%u dmaAddr=%p\n",
                        chId, type, bufB, bps, (void*)dmaAddr);
                // Capture output DMA address (chId >= 0x52 is output for UMC)
                if (chId >= 0x52 && !g_outDmaAddr) {
                    g_outDmaAddr = (volatile BYTE*)dmaAddr;
                    g_outBufBytes = bufB;
                    fprintf(g_log, "  >>> Captured OUTPUT DMA addr: %p (%u bytes)\n", (void*)dmaAddr, bufB);
                } else if (chId < 0x52 && !g_inDmaAddr) {
                    g_inDmaAddr = (volatile BYTE*)dmaAddr;
                    g_inBufBytes = bufB;
                    fprintf(g_log, "  >>> Captured INPUT DMA addr: %p (%u bytes)\n", (void*)dmaAddr, bufB);
                }
            }

            // Special: log ENABLE_STREAM
            if (code == 0x808828C0) {
                InterlockedIncrement(&g_enableStreamCalled);
                if (in && inSz >= 4) {
                    fprintf(g_log, "  ENABLE detail: value=%u\n", *(DWORD*)in);
                }
            }

            // Special: log SET_CALLBACKS and capture ctrlPage/events
            if (code == 0x80882880 && in && inSz >= 32) {
                BYTE* p = (BYTE*)in;
                UINT64 ctrlAddr = *(UINT64*)&p[0];
                UINT64 evt0 = *(UINT64*)&p[8];
                UINT64 evt1 = *(UINT64*)&p[16];
                UINT64 evt2 = *(UINT64*)&p[24];
                fprintf(g_log, "  CALLBACKS: ctrlPage=%p evt0=%p evt1=%p evt2=%p\n",
                        (void*)ctrlAddr, (void*)evt0, (void*)evt1, (void*)evt2);
                g_capturedCtrlPage = (volatile BYTE*)ctrlAddr;
                g_capturedEvents[0] = (HANDLE)evt0;
                g_capturedEvents[1] = (HANDLE)evt1;
                g_capturedEvents[2] = (HANDLE)evt2;
                fprintf(g_log, "  >>> Captured ctrlPage=%p events={%p,%p,%p}\n",
                        (void*)ctrlAddr, (void*)evt0, (void*)evt1, (void*)evt2);
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
// Dump ctrlPage content
// =========================================================================
static void dump_ctrl_page(const char* label) {
    if (!g_capturedCtrlPage || !g_log) return;
    fprintf(g_log, "\n[CTRL] %s ctrlPage dump (first 64 bytes):\n  ", label);
    for (int i = 0; i < 64; i++) {
        fprintf(g_log, "%02X ", g_capturedCtrlPage[i]);
        if ((i+1) % 32 == 0) fprintf(g_log, "\n  ");
    }
    // Interpret key fields
    DWORD* d = (DWORD*)g_capturedCtrlPage;
    fprintf(g_log, "\n  DWORDs: [0]=%u [1]=%u [2]=%u [3]=%u [4]=%u [5]=%u [6]=%u [7]=%u\n",
            d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
    fprintf(g_log, "  [8]=%u [9]=%u [10]=%u [11]=%u [12]=%u [13]=%u [14]=%u [15]=%u\n",
            d[8], d[9], d[10], d[11], d[12], d[13], d[14], d[15]);
    fflush(g_log);
}

// =========================================================================
// Dump DMA buffer content
// =========================================================================
static void dump_dma(const char* label, volatile BYTE* addr, long bytes) {
    if (!addr || !g_log || bytes <= 0) return;
    long* samples = (long*)addr;
    long numSamples = bytes / 4;
    int nonZero = 0;
    long maxSamp = numSamples < 128 ? numSamples : 128;
    for (long i = 0; i < maxSamp; i++) {
        if (samples[i] != 0) nonZero++;
    }
    fprintf(g_log, "[DMA] %s: nonZero=%d/%ld first8=[%08X %08X %08X %08X  %08X %08X %08X %08X]\n",
            label, nonZero, maxSamp,
            samples[0], samples[1], samples[2], samples[3],
            samples[4], samples[5], samples[6], samples[7]);
    fflush(g_log);
}

// =========================================================================
// main
// =========================================================================
int main() {
    printf("=== Spy v5 - Improved playback IOCTL trace ===\n");
    printf("Focus: ctrlPage monitoring + event state + DMA content\n\n");

    QueryPerformanceFrequency(&g_perfFreq);
    QueryPerformanceCounter(&g_startTick);

    g_log = fopen("D:\\Autigravity\\UMCasio\\ioctl_trace5.log", "w");
    if (!g_log) { printf("Cannot open log file\n"); return 1; }
    fprintf(g_log, "=== TUSBAUDIO Trace v5 (Improved Playback) ===\n");
    fprintf(g_log, "Focus: ctrlPage + events + DMA + ENABLE_STREAM detection\n\n");

    // Load official DLL
    HMODULE hDll = LoadLibraryW(L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioasio_x64.dll");
    if (!hDll) {
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

    double sampleRate = 0;
    ((fn_getSampleRate)vt[13])(pU, &sampleRate);
    printf("Sample rate: %.0f Hz\n", sampleRate);

    long minB, maxB, prefB, gran;
    ((fn_getBufferSize)vt[11])(pU, &minB, &maxB, &prefB, &gran);
    printf("Buffer: min=%ld max=%ld pref=%ld gran=%ld\n", minB, maxB, prefB, gran);
    fprintf(g_log, "Channels: %ld in / %ld out  SR=%.0f  BufSize=%ld\n", nIn, nOut, sampleRate, prefB);

    // Dump ctrlPage BEFORE createBuffers
    dump_ctrl_page("AFTER INIT");

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
            long cnt = InterlockedIncrement(&g_switchCount);
            g_lastBufIdx = idx;

            // Write sine wave into output buffer during callback!
            // if (g_pBI && g_bufSize > 0) { ... commented out ... }

            // Log first few callbacks
            /*
            if (cnt <= 10 && g_log) {
                fprintf(g_log, "[CB] +%.1fms bufferSwitch(idx=%ld, directProcess=%ld) #%ld\n",
                        elapsed_ms(), idx, directProcess, cnt);
                fflush(g_log);
            }
            */
        }
        static void sampleRateDidChange(double r) {
            /*
            if (g_log) {
                fprintf(g_log, "[CB] sampleRateDidChange(%.0f)\n", r);
                fflush(g_log);
            }
            */
        }
        static long asioMessage(long sel, long val, void* msg, double* opt) {
            /*
            if (g_log) {
                fprintf(g_log, "[CB] asioMessage(sel=%ld, val=%ld)\n", sel, val);
                fflush(g_log);
            }
            */
            if (sel == 1) return 1; // kAsioSelectorSupported
            if (sel == 7) return 1; // kAsioSupportsTimeInfo
            return 0;
        }
        static void* bufferSwitchTimeInfo(void* params, long idx, long directProcess) {
            long cnt = InterlockedIncrement(&g_switchCount);
            g_lastBufIdx = idx;

            // Write sine wave into output buffer during callback!
            if (g_pBI && g_bufSize > 0) {
                void* outBuf = g_pBI[1].buffers[idx];
                if (outBuf) {
                    long* samples = (long*)outBuf;
                    static double phase = 0.0;
                    double freq = 1000.0;
                    double sr = 48000.0;
                    double amp = 0.5 * 2147483647.0; // 32-bit audio amplitude
                    for (long i = 0; i < g_bufSize; i++) {
                        double val = amp * sin(phase);
                        samples[i] = (long)val;
                        phase += 2.0 * 3.14159265358979 * freq / sr;
                        if (phase > 2.0 * 3.14159265358979) phase -= 2.0 * 3.14159265358979;
                    }
                }
            }

            return params;
        }
    };

    ASIOCallbacks cbs = { Callbacks::bufferSwitch, Callbacks::sampleRateDidChange,
                          Callbacks::asioMessage, Callbacks::bufferSwitchTimeInfo };

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
    // long orr = ((fn_outputReady)vt[22])(pU);
    // printf("outputReady: %ld\n", orr);
    // fprintf(g_log, "outputReady: %ld\n\n", orr);

    // Dump ctrlPage AFTER createBuffers
    dump_ctrl_page("AFTER CREATE_BUFFERS");

    // Check event states before start
    fprintf(g_log, "\n[EVT_STATE] Before start:\n");
    for (int i = 0; i < 3; i++) {
        if (g_capturedEvents[i]) {
            DWORD w = WaitForSingleObject(g_capturedEvents[i], 0);
            fprintf(g_log, "  evt[%d] (%p): %s\n", i, g_capturedEvents[i],
                    w == WAIT_OBJECT_0 ? "SIGNALED" :
                    w == WAIT_TIMEOUT ? "NOT_SIGNALED" : "ERROR");
        }
    }
    fflush(g_log);

    // ====================================================================
    // Step 3: start
    // ====================================================================
    fprintf(g_log, "\n========== PHASE 3: START ==========\n");
    printf("\n--- start() ---\n");

    long sr2 = ((fn_start)vt[7])(pU);
    printf("start: %ld (0=OK)\n", sr2);
    fprintf(g_log, "\nstart result: %ld\n", sr2);

    if (sr2 != 0) { printf("START FAILED\n"); return 1; }

    // Dump ctrlPage immediately AFTER start
    dump_ctrl_page("IMMEDIATELY AFTER START");

    // Check event states after start
    fprintf(g_log, "\n[EVT_STATE] After start:\n");
    for (int i = 0; i < 3; i++) {
        if (g_capturedEvents[i]) {
            DWORD w = WaitForSingleObject(g_capturedEvents[i], 0);
            fprintf(g_log, "  evt[%d] (%p): %s\n", i, g_capturedEvents[i],
                    w == WAIT_OBJECT_0 ? "SIGNALED" :
                    w == WAIT_TIMEOUT ? "NOT_SIGNALED" : "ERROR");
        }
    }
    fflush(g_log);

    // ====================================================================
    // Step 4: Monitor for 5 seconds
    // ====================================================================
    fprintf(g_log, "\n========== PHASE 4: MONITORING (5 seconds) ==========\n");
    printf("\n--- Monitoring for 5 seconds... ---\n");

    DWORD prevCounter = 0;
    if (g_capturedCtrlPage) {
        prevCounter = *(DWORD*)g_capturedCtrlPage;
    }

    for (int tick = 0; tick < 20; tick++) {
        printf("DEBUG pre-sleep tick=%d\n", tick); fflush(stdout);
        Sleep(250);
        printf("DEBUG post-sleep tick=%d\n", tick); fflush(stdout);
        long sc = g_switchCount;
        long wfb = g_wfbCount;

        // Read ctrlPage counter
        DWORD curCounter = 0;
        if (g_capturedCtrlPage) {
            curCounter = *(DWORD*)g_capturedCtrlPage;
        }

        printf("  +%.1fs: bsCnt=%ld WFB=%ld(ok=%ld pend=%ld) lastIdx=%ld ctrlCounter=%u(delta=%d)\n",
               (tick+1)*0.25, sc, wfb, (long)g_wfbOkCount, (long)g_wfbPendingCount,
               (long)g_lastBufIdx, curCounter, (int)(curCounter - prevCounter));

        // Log every second (every 4 ticks)
        if (tick % 4 == 3) {
            fprintf(g_log, "\n[STATUS] +%.1fs: switchCount=%ld WFB=%ld(ok=%ld,pend=%ld) ctrlCounter=%u(delta=%d) ENABLE_STREAM_called=%ld\n",
                    (tick+1)*0.25, sc, wfb, (long)g_wfbOkCount, (long)g_wfbPendingCount,
                    curCounter, (int)(curCounter - prevCounter), (long)g_enableStreamCalled);
            dump_ctrl_page("PERIODIC");

            // Dump DMA buffers
            if (g_outDmaAddr) {
                dump_dma("OUT half0", g_outDmaAddr, g_outBufBytes / 2);
                dump_dma("OUT half1", g_outDmaAddr + g_outBufBytes / 2, g_outBufBytes / 2);
            }
            if (g_inDmaAddr) {
                dump_dma("IN  half0", g_inDmaAddr, g_inBufBytes / 2);
                dump_dma("IN  half1", g_inDmaAddr + g_inBufBytes / 2, g_inBufBytes / 2);
            }

            // Check event states periodically
            fprintf(g_log, "[EVT_STATE] Periodic check:\n");
            for (int i = 0; i < 3; i++) {
                if (g_capturedEvents[i]) {
                    DWORD w = WaitForSingleObject(g_capturedEvents[i], 0);
                    fprintf(g_log, "  evt[%d] (%p): %s\n", i, g_capturedEvents[i],
                            w == WAIT_OBJECT_0 ? "SIGNALED" :
                            w == WAIT_TIMEOUT ? "NOT_SIGNALED" : "ERROR");
                }
            }
            fflush(g_log);
        }

        // Also dump output buffer content from ASIO perspective
        if (tick % 4 == 3 && bi[1].buffers[0]) {
            int useIdx = g_lastBufIdx >= 0 ? (int)g_lastBufIdx : 0;
            long* outSamples = (long*)bi[1].buffers[useIdx];
            int nonZero = 0;
            for (long i = 0; i < prefB && i < 128; i++) {
                if (outSamples[i] != 0) nonZero++;
            }
            printf("           ASIO Out buf[%d]: nonZero=%d/%ld  first4=[%08X %08X %08X %08X]\n",
                   useIdx, nonZero, prefB < 128 ? prefB : 128L,
                   outSamples[0], outSamples[1], outSamples[2], outSamples[3]);
        }

        prevCounter = curCounter;
    }

    // Final WFB stats
    fprintf(g_log, "\n========== FINAL STATS ==========\n");
    fprintf(g_log, "Total WFB calls: %ld\n", (long)g_wfbCount);
    fprintf(g_log, "WFB OK (immediate): %ld\n", (long)g_wfbOkCount);
    fprintf(g_log, "WFB PENDING: %ld\n", (long)g_wfbPendingCount);
    fprintf(g_log, "ENABLE_STREAM called: %ld\n", (long)g_enableStreamCalled);
    fprintf(g_log, "Total bufferSwitch callbacks: %ld\n", (long)g_switchCount);

    // ====================================================================
    // Step 5: Stop
    // ====================================================================
    fprintf(g_log, "\n========== PHASE 5: STOP ==========\n");
    printf("\n--- stop() ---\n");
    ((fn_stop)vt[8])(pU);

    dump_ctrl_page("AFTER STOP");

    fprintf(g_log, "\n========== PHASE 6: DISPOSE ==========\n");
    printf("--- disposeBuffers() ---\n");
    ((fn_disposeBuffers)vt[20])(pU);

    // Cleanup
    pU->Release();

    fprintf(g_log, "\n========== SUMMARY ==========\n");
    fprintf(g_log, "Events created: %d\n", g_evtCount);
    fprintf(g_log, "bufferSwitch count: %ld\n", (long)g_switchCount);
    fprintf(g_log, "WAIT_FOR_BUFFER total: %ld\n", (long)g_wfbCount);
    fprintf(g_log, "ENABLE_STREAM called: %ld\n", (long)g_enableStreamCalled);
    fprintf(g_log, "\nKey findings:\n");
    if (g_enableStreamCalled == 0)
        fprintf(g_log, "  !!! ENABLE_STREAM was NEVER called by official DLL !!!\n");
    if (g_switchCount == 0)
        fprintf(g_log, "  !!! bufferSwitch was NEVER called - callback issue !!!\n");
    if (g_wfbCount == 0)
        fprintf(g_log, "  !!! WAIT_FOR_BUFFER was NEVER called !!!\n");
    fprintf(g_log, "\nDone.\n");
    fclose(g_log);

    printf("\n=== Done! See ioctl_trace5.log ===\n");
    printf("Key results:\n");
    printf("  ENABLE_STREAM called: %ld\n", (long)g_enableStreamCalled);
    printf("  bufferSwitch count: %ld\n", (long)g_switchCount);
    printf("  WAIT_FOR_BUFFER count: %ld\n", (long)g_wfbCount);
    printf("  ctrlPage was: %s\n", g_capturedCtrlPage ? "CAPTURED" : "NOT CAPTURED");
    return 0;
}
