/*
 * TusbAudioDirect.h - TUSBAUDIO 专有 IOCTL 直连引擎
 *
 * 通过逆向分析的 TUSBAUDIO 协议直接和 umc_audio.sys 通信，
 * 使用 DMA 内存映射传输音频数据。
 *
 * 完整协议 (2026-03-21 逆向确认):
 *   init:  GET_DEVICE_INFO → GET_STATUS → GET_DEVICE_PROPS → SET_MODE
 *          → SET_CALLBACKS → GET_STREAM_CONFIG → GET_CHANNEL_LIST → GET_CHANNEL_INFO×N
 *   create: SET_BUFFER_SIZE → (SELECT_CHANNEL + MAP_CHANNEL_BUFFER)×N
 *   start:  START_STREAMING → ENABLE_STREAM(bufSz) → 线程循环 WAIT_FOR_BUFFER
 *   stop:   STOP_STREAMING → DESELECT_CHANNEL×N
 *
 * IOCTL DeviceType: 0x8088 (TUSBAUDIO proprietary)
 */

#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <thread>

// ============================================================================
// TUSBAUDIO IOCTL codes (reversed from umc_audioasio_x64.dll)
// ============================================================================
#define TUSB_IOCTL_GET_DEVICE_INFO      0x80882004  // Func=2049
#define TUSB_IOCTL_GET_DEVICE_PROPS     0x808820C4  // Func=2097
#define TUSB_IOCTL_SET_MODE             0x80882804  // Func=2561
#define TUSB_IOCTL_GET_STREAM_CONFIG    0x80882808  // Func=2562
#define TUSB_IOCTL_GET_CHANNEL_LIST     0x8088280C  // Func=2563
#define TUSB_IOCTL_GET_CHANNEL_INFO     0x80882810  // Func=2564
#define TUSB_IOCTL_GET_STATUS           0x80882820  // Func=2568
#define TUSB_IOCTL_SET_BUFFER_SIZE      0x80882824  // Func=2569
#define TUSB_IOCTL_SELECT_CHANNEL       0x80882840  // Func=2576
#define TUSB_IOCTL_DESELECT_CHANNEL     0x80882844  // Func=2577
#define TUSB_IOCTL_SET_CALLBACKS        0x80882880  // Func=2592
#define TUSB_IOCTL_MAP_CHANNEL_BUFFER   0x808828A0  // Func=2600
#define TUSB_IOCTL_ENABLE_STREAM        0x808828C0  // Func=2608 🔑 激活DMA传输
#define TUSB_IOCTL_STOP_STREAMING       0x808828C4  // Func=2609 真正的STOP
#define TUSB_IOCTL_START_STREAMING      0x808828C8  // Func=2610
#define TUSB_IOCTL_WAIT_FOR_BUFFER      0x808828F4  // Func=2621 🔑 阻塞轮询

// Channel descriptor (from GET_CHANNEL_LIST / GET_CHANNEL_INFO)
struct TusbChannel {
    DWORD direction;    // always 1 in list (use role from GET_CHANNEL_INFO)
    DWORD streamId;     // e.g. 0x0114
    DWORD channelId;    // e.g. 0x40=In1, 0x52=Out1
    DWORD type;         // e.g. 0x0117
    BYTE  rawEntry[16]; // raw 16-byte entry for SELECT/DESELECT
    char  name[64];     // UTF-8 name ("In 1", "Out 1", etc.)
    void* dmaBuffer;    // Mapped DMA buffer address (buf[0])
    void* dmaBuffer2;   // Second buffer (buf[1] = buf[0] + bufBytes)
    DWORD dmaBufferSize;// Size in bytes per buffer half
};

// Stream config (from GET_STREAM_CONFIG)
struct TusbStreamConfig {
    DWORD currentRate;
    DWORD numSupportedRates;
    DWORD supportedRates[16];
};

// Device info (from GET_DEVICE_INFO / GET_DEVICE_PROPS)
struct TusbDeviceInfo {
    DWORD vid;
    DWORD pid;
    char  serial[64];
};

// Buffer switch callback: (bufferIndex, userData)
typedef void (*TusbBufferSwitchCallback)(long bufferIndex, void* userData);

class TusbAudioDirect {
public:
    TusbAudioDirect();
    ~TusbAudioDirect();

    // Open device via TUSBAUDIO interface GUID
    bool open();
    void close();
    bool isOpen() const { return m_hDevice != INVALID_HANDLE_VALUE; }

    // Initialize: query device info, channels, stream config
    bool init();

    // Buffer management
    bool createBuffers(const std::vector<int>& inputChannels,
                       const std::vector<int>& outputChannels,
                       DWORD bufferSize);
    void disposeBuffers();

    // Streaming
    bool start();
    bool stop();
    bool isRunning() const { return m_running.load(); }

    // Set buffer switch callback
    void setBufferSwitchCallback(TusbBufferSwitchCallback cb, void* userData) {
        m_callback = cb;
        m_callbackUserData = userData;
    }

    // Getters
    const TusbDeviceInfo& getDeviceInfo() const { return m_deviceInfo; }
    const TusbStreamConfig& getStreamConfig() const { return m_streamConfig; }
    const std::vector<TusbChannel>& getInputChannels() const { return m_inputChannels; }
    const std::vector<TusbChannel>& getOutputChannels() const { return m_outputChannels; }
    DWORD getBufferSize() const { return m_bufferSize; }

    // Get DMA buffer pointers (buf[0] and buf[1] for double buffering)
    // Data format: 32-bit INT (24-bit PCM left-aligned in 32-bit)
    void* getChannelBuffer(bool isInput, int channelIndex, int bufferHalf) const;

    // Get current buffer counter (from control page)
    DWORD getBufferCounter() const;

private:
    HANDLE m_hDevice;
    TusbDeviceInfo m_deviceInfo;
    TusbStreamConfig m_streamConfig;
    std::vector<TusbChannel> m_inputChannels;
    std::vector<TusbChannel> m_outputChannels;
    DWORD m_bufferSize;
    std::atomic<bool> m_running;

    // Events (matching official DLL pattern)
    HANDLE m_eventsAuto[3];    // AUTO-RESET for SET_CALLBACKS
    HANDLE m_eventsManual[6];  // MANUAL-RESET
    HANDLE m_eventOverlapped;  // for OVERLAPPED IO

    // Control page (shared with kernel driver)
    BYTE* m_ctrlPage;

    // Selected channel indices
    std::vector<int> m_selectedInputs;
    std::vector<int> m_selectedOutputs;

    // DMA buffer memory
    std::vector<void*> m_dmaAllocations;

    // Callback
    TusbBufferSwitchCallback m_callback;
    void* m_callbackUserData;

    // Audio poll thread
    std::thread m_pollThread;
    void pollThreadFunc();

    // IOCTL helper (OVERLAPPED mode)
    OVERLAPPED m_ov;
    bool sendIoctl(DWORD code, const void* inBuf = nullptr, DWORD inSize = 0,
                   void* outBuf = nullptr, DWORD outSize = 0, DWORD* returned = nullptr);
    bool queryChannelList();
    bool queryChannelInfo(TusbChannel& ch);

    // Device discovery
    std::wstring findDevicePath();
};
