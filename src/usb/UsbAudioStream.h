/*
 * UsbAudioStream.h - USB 等时传输音频流引擎
 * 负责管理 UAC2 Isochronous IN/OUT 传输，提供与 ASIO 的数据桥接
 */

#pragma once
#include <vector>
#include <atomic>
#include <thread>
#include "libusb.h"
#include "UsbAudioDevice.h"
#include "../driver/AudioBuffer.h"

// URB (USB Request Block) 配置
const int NUM_ISO_TRANSFERS = 8;
const int PACKETS_PER_TRANSFER = 8; // Each frame is 125us => 8 packets = 1ms

class UsbAudioStream {
public:
    UsbAudioStream(UsbAudioDevice* device);
    ~UsbAudioStream();

    // 启动/停止流
    bool start(uint32_t sampleRate, uint32_t bufferFrames, AudioBuffer* asioBuffer);
    void stop();

    bool isRunning() const { return m_running; }

    // WFB 阻塞同步机制 (供 ASIO 回调使用)
    bool waitForBuffer(int* nextIndex);
    void ackBuffer();

private:
    UsbAudioDevice* m_device;
    std::atomic<bool> m_running;
    std::thread m_workerThread;

    AudioBuffer* m_asioBuffer;
    int m_bufferSizeFrames;
    int m_currentAsioIndex;
    
    // 同步原语
    HANDLE m_bufferEvent;
    
    // USB 传输上下文
    struct TransferContext {
        UsbAudioStream* stream;
        libusb_transfer* transfer;
        uint8_t* buffer;
    };

    std::vector<TransferContext*> m_inTransfers;
    std::vector<TransferContext*> m_outTransfers;
    std::vector<TransferContext*> m_fbTransfers;

    // ISO 端点信息
    AudioEndpointInfo m_inEp;
    AudioEndpointInfo m_outEp;

    // 分配与释放资源
    bool allocTransfers();
    void freeTransfers();

    // 提交下一个传输
    bool submitTransfer(libusb_transfer* transfer);

    // libusb 轮询线程
    void eventThreadFunc();

    // 传输完成回调 (Static)
    static void LIBUSB_CALL inCallback(struct libusb_transfer* transfer);
    static void LIBUSB_CALL outCallback(struct libusb_transfer* transfer);
    static void LIBUSB_CALL fbCallback(struct libusb_transfer* transfer);

    // 实际处理函数
    void handleInTransfer(struct libusb_transfer* transfer);
    void handleOutTransfer(struct libusb_transfer* transfer);
    
    // 格式转换 (USB payload <-> ASIO buffer)
    void decodePayload(const uint8_t* payload, int payloadLen);
    void encodePayload(uint8_t* payload, int targetLen);

    // 时钟计数器
    std::atomic<uint64_t> m_samplesCaptured;
    std::atomic<uint64_t> m_samplesRendered;
};
