/*
 * TusbAudioDirect.cpp - TUSBAUDIO 专有 IOCTL 直连引擎实现
 *
 * 完整协议已逆向确认 (2026-03-21):
 *   - 所有 IOCTL 使用 OVERLAPPED IO
 *   - START 后必须调 ENABLE_STREAM(bufSz) 激活 DMA
 *   - 音频线程循环调 WAIT_FOR_BUFFER 检测 ctrl 计数器变化
 *   - 数据格式: 32-bit INT (24-bit PCM 左对齐)
 *   - 双缓冲: buf[1] = buf[0] + bufferSize * 4
 */

#include "TusbAudioDirect.h"
#include "../utils/Logger.h"
#include <setupapi.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include <initguid.h>
#include <cstring>
#include <cstdio>

#pragma comment(lib, "setupapi.lib")

#define LOG_MODULE "TusbDirect"

// TUSBAUDIO device interface GUID
// {215A80EF-69BD-4D85-AC71-0C6EA6E6BE17}
static const GUID GUID_TUSBAUDIO_DEVICE =
    {0x215A80EF, 0x69BD, 0x4D85, {0xAC, 0x71, 0x0C, 0x6E, 0xA6, 0xE6, 0xBE, 0x17}};

#define DMA_ALLOC_SIZE 0x10000  // 64KB per channel
#define CTRL_PAGE_SIZE 0x10000  // 64KB control page

// ============================================================================
// Constructor / Destructor
// ============================================================================

TusbAudioDirect::TusbAudioDirect()
    : m_hDevice(INVALID_HANDLE_VALUE)
    , m_bufferSize(128)
    , m_running(false)
    , m_ctrlPage(nullptr)
    , m_callback(nullptr)
    , m_callbackUserData(nullptr)
    , m_eventOverlapped(NULL)
{
    memset(&m_deviceInfo, 0, sizeof(m_deviceInfo));
    memset(&m_streamConfig, 0, sizeof(m_streamConfig));
    memset(&m_ov, 0, sizeof(m_ov));
    memset(m_eventsAuto, 0, sizeof(m_eventsAuto));
    memset(m_eventsManual, 0, sizeof(m_eventsManual));
}

TusbAudioDirect::~TusbAudioDirect() {
    if (m_running) stop();
    disposeBuffers();
    close();
}

// ============================================================================
// Device Discovery
// ============================================================================

std::wstring TusbAudioDirect::findDevicePath() {
    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_TUSBAUDIO_DEVICE, nullptr, nullptr,
                                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return L"";

    SP_DEVICE_INTERFACE_DATA ifData = {};
    ifData.cbSize = sizeof(ifData);
    std::wstring result;

    if (SetupDiEnumDeviceInterfaces(devInfo, nullptr, &GUID_TUSBAUDIO_DEVICE, 0, &ifData)) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &reqSize, nullptr);
        auto* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)calloc(1, reqSize);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, reqSize, nullptr, nullptr)) {
            result = detail->DevicePath;
        }
        free(detail);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
}

// ============================================================================
// Open / Close
// ============================================================================

bool TusbAudioDirect::open() {
    if (m_hDevice != INVALID_HANDLE_VALUE) close();

    // Create events (matching official DLL pattern: 3 AUTO + 6 MANUAL + 1 OVERLAPPED)
    for (int i = 0; i < 3; i++)
        m_eventsAuto[i] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    for (int i = 0; i < 6; i++)
        m_eventsManual[i] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_eventOverlapped = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    std::wstring path = findDevicePath();
    if (path.empty()) {
        LOG_ERROR(LOG_MODULE, "No TUSBAUDIO device found");
        return false;
    }

    // Open with FILE_FLAG_OVERLAPPED (required by TUSBAUDIO driver)
    m_hDevice = CreateFileW(path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, nullptr);

    if (m_hDevice == INVALID_HANDLE_VALUE) {
        LOG_ERROR(LOG_MODULE, "CreateFileW failed: %lu", GetLastError());
        return false;
    }

    LOG_INFO(LOG_MODULE, "Device opened (OVERLAPPED mode)");
    return true;
}

void TusbAudioDirect::close() {
    if (m_hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hDevice);
        m_hDevice = INVALID_HANDLE_VALUE;
    }
    for (int i = 0; i < 3; i++) { if (m_eventsAuto[i]) { CloseHandle(m_eventsAuto[i]); m_eventsAuto[i] = NULL; } }
    for (int i = 0; i < 6; i++) { if (m_eventsManual[i]) { CloseHandle(m_eventsManual[i]); m_eventsManual[i] = NULL; } }
    if (m_eventOverlapped) { CloseHandle(m_eventOverlapped); m_eventOverlapped = NULL; }

    if (m_ctrlPage) { VirtualFree(m_ctrlPage, 0, MEM_RELEASE); m_ctrlPage = nullptr; }
}

// ============================================================================
// IOCTL Helper (OVERLAPPED mode, matching official DLL)
// ============================================================================

bool TusbAudioDirect::sendIoctl(DWORD code, const void* inBuf, DWORD inSize,
                                 void* outBuf, DWORD outSize, DWORD* returned) {
    if (m_hDevice == INVALID_HANDLE_VALUE) return false;

    memset(&m_ov, 0, sizeof(m_ov));
    m_ov.hEvent = m_eventOverlapped;

    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl(m_hDevice, code,
        (LPVOID)inBuf, inSize, outBuf, outSize,
        &bytesReturned, &m_ov);

    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            ok = GetOverlappedResult(m_hDevice, &m_ov, &bytesReturned, TRUE);
        }
        if (!ok) {
            LOG_ERROR(LOG_MODULE, "IOCTL 0x%08X failed: %lu", code, GetLastError());
            return false;
        }
    }

    if (returned) *returned = bytesReturned;
    return true;
}

// ============================================================================
// Initialization
// ============================================================================

bool TusbAudioDirect::init() {
    if (!isOpen()) {
        if (!open()) return false;
    }

    // Step 1: GET_DEVICE_INFO
    BYTE devInfoBuf[24] = {};
    sendIoctl(TUSB_IOCTL_GET_DEVICE_INFO, nullptr, 0, devInfoBuf, 24);

    // Step 2: GET_STATUS
    DWORD status = 0;
    sendIoctl(TUSB_IOCTL_GET_STATUS, nullptr, 0, &status, 4);

    // Step 3: GET_DEVICE_PROPERTIES
    BYTE propsBuf[1040] = {};
    DWORD ret = 0;
    if (sendIoctl(TUSB_IOCTL_GET_DEVICE_PROPS, nullptr, 0, propsBuf, sizeof(propsBuf), &ret)) {
        m_deviceInfo.vid = *(WORD*)&propsBuf[0];
        m_deviceInfo.pid = *(WORD*)&propsBuf[4];
        WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)&propsBuf[12], -1,
                            m_deviceInfo.serial, sizeof(m_deviceInfo.serial), nullptr, nullptr);
        LOG_INFO(LOG_MODULE, "Device: VID=0x%04X PID=0x%04X Serial=%s",
                 m_deviceInfo.vid, m_deviceInfo.pid, m_deviceInfo.serial);
    }

    // Step 4: SET_MODE (ASIO mode = 0)
    DWORD mode = 0;
    sendIoctl(TUSB_IOCTL_SET_MODE, &mode, 4);

    // Step 5: SET_CALLBACKS (control page + 3 AUTO events)
    m_ctrlPage = (BYTE*)VirtualAlloc(nullptr, CTRL_PAGE_SIZE,
                                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!m_ctrlPage) {
        LOG_ERROR(LOG_MODULE, "Failed to allocate control page");
        return false;
    }

    BYTE cbBuf[32] = {};
    *(UINT64*)&cbBuf[0]  = (UINT64)(uintptr_t)m_ctrlPage;
    *(UINT64*)&cbBuf[8]  = (UINT64)(uintptr_t)m_eventsAuto[0];
    *(UINT64*)&cbBuf[16] = (UINT64)(uintptr_t)m_eventsAuto[1];
    *(UINT64*)&cbBuf[24] = (UINT64)(uintptr_t)m_eventsAuto[2];
    sendIoctl(TUSB_IOCTL_SET_CALLBACKS, cbBuf, 32);

    // Step 6: GET_STREAM_CONFIG
    BYTE configBuf[292] = {};
    if (sendIoctl(TUSB_IOCTL_GET_STREAM_CONFIG, nullptr, 0, configBuf, sizeof(configBuf), &ret)) {
        m_streamConfig.currentRate = *(DWORD*)&configBuf[0];
        m_streamConfig.numSupportedRates = *(DWORD*)&configBuf[4];
        for (DWORD i = 0; i < m_streamConfig.numSupportedRates && i < 16; i++) {
            m_streamConfig.supportedRates[i] = *(DWORD*)&configBuf[8 + i * 4];
        }
        LOG_INFO(LOG_MODULE, "Rate: %u Hz, %u supported rates",
                 m_streamConfig.currentRate, m_streamConfig.numSupportedRates);
    }

    // Step 7: GET_CHANNEL_LIST + GET_CHANNEL_INFO
    if (!queryChannelList()) {
        LOG_ERROR(LOG_MODULE, "Channel query failed");
        return false;
    }

    // Second stream config query (matching official DLL)
    sendIoctl(TUSB_IOCTL_GET_STREAM_CONFIG, nullptr, 0, configBuf, sizeof(configBuf));

    LOG_INFO(LOG_MODULE, "Init OK: %zu inputs, %zu outputs",
             m_inputChannels.size(), m_outputChannels.size());
    return true;
}

bool TusbAudioDirect::queryChannelList() {
    m_inputChannels.clear();
    m_outputChannels.clear();

    BYTE listBuf[8200] = {};
    DWORD ret = 0;
    if (!sendIoctl(TUSB_IOCTL_GET_CHANNEL_LIST, nullptr, 0, listBuf, sizeof(listBuf), &ret)) {
        return false;
    }

    auto parseBlock = [&](DWORD blockOffset, DWORD numCh, bool isInput) {
        for (DWORD i = 0; i < numCh; i++) {
            DWORD off = blockOffset + 4 + i * 16;
            if (off + 16 > ret) break;

            TusbChannel ch = {};
            memcpy(ch.rawEntry, &listBuf[off], 16);
            ch.direction = *(DWORD*)&listBuf[off + 0];
            ch.streamId  = *(DWORD*)&listBuf[off + 4];
            ch.channelId = *(DWORD*)&listBuf[off + 8];
            ch.type      = *(DWORD*)&listBuf[off + 12];
            ch.dmaBuffer = nullptr;
            ch.dmaBuffer2 = nullptr;
            ch.dmaBufferSize = 0;

            queryChannelInfo(ch);

            if (isInput) m_inputChannels.push_back(ch);
            else m_outputChannels.push_back(ch);
        }
    };

    DWORD numInputs = *(DWORD*)&listBuf[0];
    parseBlock(0, numInputs, true);

    if (ret >= 4104) {
        DWORD numOutputs = *(DWORD*)&listBuf[4100];
        parseBlock(4100, numOutputs, false);
    }

    LOG_INFO(LOG_MODULE, "Channels: %zu in, %zu out",
             m_inputChannels.size(), m_outputChannels.size());
    return true;
}

bool TusbAudioDirect::queryChannelInfo(TusbChannel& ch) {
    BYTE outBuf[108] = {};
    DWORD ret = 0;
    if (!sendIoctl(TUSB_IOCTL_GET_CHANNEL_INFO, ch.rawEntry, 16, outBuf, 108, &ret)) {
        snprintf(ch.name, sizeof(ch.name), "Ch 0x%02X", ch.channelId);
        return false;
    }

    wchar_t wname[32] = {};
    memcpy(wname, &outBuf[24], 64);
    WideCharToMultiByte(CP_UTF8, 0, wname, -1, ch.name, sizeof(ch.name), nullptr, nullptr);
    return true;
}

// ============================================================================
// Buffer Management
// ============================================================================

bool TusbAudioDirect::createBuffers(const std::vector<int>& inputChannels,
                                     const std::vector<int>& outputChannels,
                                     DWORD bufferSize) {
    disposeBuffers();
    m_bufferSize = bufferSize;
    m_selectedInputs = inputChannels;
    m_selectedOutputs = outputChannels;

    DWORD bufBytes = bufferSize * 4; // 32-bit samples

    // SET_BUFFER_SIZE
    DWORD sz = bufferSize;
    if (!sendIoctl(TUSB_IOCTL_SET_BUFFER_SIZE, &sz, 4)) {
        LOG_ERROR(LOG_MODULE, "SET_BUFFER_SIZE failed");
        return false;
    }

    // SELECT + MAP each channel
    auto setupChannel = [&](TusbChannel& ch) -> bool {
        // SELECT_CHANNEL
        if (!sendIoctl(TUSB_IOCTL_SELECT_CHANNEL, ch.rawEntry, 16)) {
            LOG_ERROR(LOG_MODULE, "SELECT ch 0x%02X failed", ch.channelId);
            return false;
        }

        void* buf = VirtualAlloc(nullptr, DMA_ALLOC_SIZE,
                                  MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!buf) return false;
        m_dmaAllocations.push_back(buf);

        // MAP_CHANNEL_BUFFER
        BYTE mapBuf[24] = {};
        *(DWORD*)&mapBuf[0] = ch.channelId;
        *(DWORD*)&mapBuf[4] = ch.type;
        *(DWORD*)&mapBuf[8] = bufBytes;
        *(DWORD*)&mapBuf[12] = 32; // bits per sample
        *(UINT64*)&mapBuf[16] = (UINT64)(uintptr_t)buf;

        if (!sendIoctl(TUSB_IOCTL_MAP_CHANNEL_BUFFER, mapBuf, 24)) {
            LOG_ERROR(LOG_MODULE, "MAP ch 0x%02X failed", ch.channelId);
            return false;
        }

        ch.dmaBuffer = buf;
        ch.dmaBuffer2 = (BYTE*)buf + bufBytes; // double buffer
        ch.dmaBufferSize = bufBytes;
        return true;
    };

    for (int idx : inputChannels) {
        if (idx >= 0 && idx < (int)m_inputChannels.size()) {
            if (!setupChannel(m_inputChannels[idx])) return false;
        }
    }
    for (int idx : outputChannels) {
        if (idx >= 0 && idx < (int)m_outputChannels.size()) {
            if (!setupChannel(m_outputChannels[idx])) return false;
        }
    }

    LOG_INFO(LOG_MODULE, "Buffers created: %zu in + %zu out, size=%u",
             inputChannels.size(), outputChannels.size(), bufferSize);
    return true;
}

void TusbAudioDirect::disposeBuffers() {
    for (void* p : m_dmaAllocations) {
        VirtualFree(p, 0, MEM_RELEASE);
    }
    m_dmaAllocations.clear();

    for (auto& ch : m_inputChannels) { ch.dmaBuffer = nullptr; ch.dmaBuffer2 = nullptr; ch.dmaBufferSize = 0; }
    for (auto& ch : m_outputChannels) { ch.dmaBuffer = nullptr; ch.dmaBuffer2 = nullptr; ch.dmaBufferSize = 0; }
    m_selectedInputs.clear();
    m_selectedOutputs.clear();
}

void* TusbAudioDirect::getChannelBuffer(bool isInput, int channelIndex, int bufferHalf) const {
    const auto& channels = isInput ? m_inputChannels : m_outputChannels;
    if (channelIndex >= 0 && channelIndex < (int)channels.size()) {
        return (bufferHalf == 0) ? channels[channelIndex].dmaBuffer
                                 : channels[channelIndex].dmaBuffer2;
    }
    return nullptr;
}

DWORD TusbAudioDirect::getBufferCounter() const {
    if (m_ctrlPage) return *(volatile DWORD*)m_ctrlPage;
    return 0;
}

// ============================================================================
// Streaming
// ============================================================================

bool TusbAudioDirect::start() {
    if (m_running) return true;

    // START_STREAMING
    if (!sendIoctl(TUSB_IOCTL_START_STREAMING)) {
        LOG_ERROR(LOG_MODULE, "START_STREAMING failed");
        return false;
    }

    // ENABLE_STREAM (pass buffer size — this activates DMA transfer!)
    DWORD sz = m_bufferSize;
    if (!sendIoctl(TUSB_IOCTL_ENABLE_STREAM, &sz, 4)) {
        LOG_ERROR(LOG_MODULE, "ENABLE_STREAM failed");
        sendIoctl(TUSB_IOCTL_STOP_STREAMING);
        return false;
    }

    // Start poll thread
    m_running = true;
    m_pollThread = std::thread(&TusbAudioDirect::pollThreadFunc, this);

    LOG_INFO(LOG_MODULE, "Streaming started (bufSize=%u, rate=%u)",
             m_bufferSize, m_streamConfig.currentRate);
    return true;
}

bool TusbAudioDirect::stop() {
    if (!m_running) return true;

    m_running = false;

    // Wake poll thread from either WaitForSingleObject or pending WFB
    if (m_eventsAuto[0]) SetEvent(m_eventsAuto[0]);
    CancelIoEx(m_hDevice, nullptr);

    if (m_pollThread.joinable()) {
        m_pollThread.join();
    }

    // STOP_STREAMING (real stop = 0x808828C4)
    sendIoctl(TUSB_IOCTL_STOP_STREAMING);

    // DESELECT all channels
    for (int idx : m_selectedInputs) {
        if (idx >= 0 && idx < (int)m_inputChannels.size())
            sendIoctl(TUSB_IOCTL_DESELECT_CHANNEL, m_inputChannels[idx].rawEntry, 16);
    }
    for (int idx : m_selectedOutputs) {
        if (idx >= 0 && idx < (int)m_outputChannels.size())
            sendIoctl(TUSB_IOCTL_DESELECT_CHANNEL, m_outputChannels[idx].rawEntry, 16);
    }

    LOG_INFO(LOG_MODULE, "Streaming stopped");
    return true;
}

void TusbAudioDirect::pollThreadFunc() {
    LOG_INFO(LOG_MODULE, "Poll thread started (official WFB-only pattern)");

    // Set timer resolution to 1ms
    timeBeginPeriod(1);

    // Boost thread priority
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    // Use MMCSS if available
    HANDLE hAvrt = nullptr;
    HMODULE hAvrtDll = LoadLibraryW(L"avrt.dll");
    if (hAvrtDll) {
        typedef HANDLE(WINAPI* fnAvSet)(LPCWSTR, LPDWORD);
        auto pAvSet = (fnAvSet)GetProcAddress(hAvrtDll, "AvSetMmThreadCharacteristicsW");
        if (pAvSet) {
            DWORD taskIndex = 0;
            hAvrt = pAvSet(L"Pro Audio", &taskIndex);
        }
    }

    DWORD lastCounter = getBufferCounter();

    // Isolated OVERLAPPED for this thread (prevent race with main thread sendIoctl)
    HANDLE hThreadEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    OVERLAPPED threadOv = {};
    threadOv.hEvent = hThreadEvent;

    DWORD startTick = timeGetTime();
    long callbackCount = 0;

    while (m_running.load()) {
        // =====================================================================
        // Timing: Event blocks → callback → WFB ACK (non-blocking)
        // WFB with timeout 0: just send ACK, don't block for next period
        // =====================================================================

        // STEP 1: Block on hardware interrupt event
        DWORD waitRes = WaitForSingleObject(m_eventsAuto[0], 1000);
        if (waitRes == WAIT_TIMEOUT) {
            if (m_running.load()) {
                LOG_ERROR(LOG_MODULE, "Hardware interrupt event timeout!");
            }
            continue;
        }
        if (!m_running.load()) break;

        // Check counter advancement
        DWORD curCounter = getBufferCounter();
        if (curCounter == lastCounter) {
            continue;
        }

        // STEP 2: Call DAW callback
        int bufferIndex = (curCounter + 1) & 1;
        lastCounter = curCounter;
        callbackCount++;

        if (m_callback) {
            m_callback(bufferIndex, m_callbackUserData);
        }

        // STEP 3: Send WAIT_FOR_BUFFER as ACK
        // 按照官方驱动逻辑，提交 WAIT_FOR_BUFFER 等待硬件中断
        DWORD bytesReturned = 0;
        ResetEvent(hThreadEvent);
        memset(&threadOv, 0, sizeof(threadOv));
        threadOv.hEvent = hThreadEvent;

        BOOL ok = DeviceIoControl(m_hDevice, TUSB_IOCTL_WAIT_FOR_BUFFER,
                                  nullptr, 0, nullptr, 0, &bytesReturned, &threadOv);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            // 直接等待 WFB 完成 （与硬件中断 m_eventsAuto 同步到达），无需粗暴地 CancelIoEx。
            // 之前的 CancelIoEx 会瞬间破坏底层的时序状态，导致整个设备（包括 WDM）发生随机的噼啪声。
            WaitForSingleObject(hThreadEvent, 500); 
        }
    }

    // Log callback rate for diagnosis
    DWORD elapsed = timeGetTime() - startTick;
    if (elapsed > 0) {
        double rate = (double)callbackCount / ((double)elapsed / 1000.0);
        LOG_INFO(LOG_MODULE, "Poll stats: %ld callbacks in %lu ms (%.1f Hz, expected %.1f Hz)",
                 callbackCount, elapsed, rate, 
                 (double)m_streamConfig.currentRate / (double)m_bufferSize);
    }

    // Cleanup
    if (hThreadEvent) CloseHandle(hThreadEvent);

    if (hAvrt && hAvrtDll) {
        typedef BOOL(WINAPI* fnAvRevert)(HANDLE);
        auto pRevert = (fnAvRevert)GetProcAddress(hAvrtDll, "AvRevertMmThreadCharacteristics");
        if (pRevert) pRevert(hAvrt);
    }
    if (hAvrtDll) FreeLibrary(hAvrtDll);

    timeEndPeriod(1);
    LOG_INFO(LOG_MODULE, "Poll thread stopped");
}
