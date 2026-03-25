/*
 * UsbAudioStream.cpp - USB 等时传输音频流引擎
 */

#include "UsbAudioStream.h"
#include "../utils/Logger.h"
#include <cstring>
#include <iostream>

using namespace std;

UsbAudioStream::UsbAudioStream(UsbAudioDevice* device) 
    : m_device(device), m_running(false), m_asioBuffer(nullptr), 
      m_bufferSizeFrames(0), m_currentAsioIndex(0) 
{
    m_bufferEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
}

UsbAudioStream::~UsbAudioStream() {
    stop();
    CloseHandle(m_bufferEvent);
}

bool UsbAudioStream::start(uint32_t sampleRate, uint32_t bufferFrames, AudioBuffer* asioBuffer) {
    if (m_running) return true;
    
    m_asioBuffer = asioBuffer;
    m_bufferSizeFrames = bufferFrames;
    m_samplesCaptured = 0;
    m_samplesRendered = 0;
    m_currentAsioIndex = 0;

    // 获取工作端点
    const auto& ins = m_device->getCaptureEndpoints();
    const auto& outs = m_device->getRenderEndpoints();
    if (ins.empty() || outs.empty()) return false;

    // UMC 1820 硬件分配 (18in, 20out @ 48kHz)
    m_inEp = ins[0];
    m_outEp = outs[0];

    // 激活对应接口的 AltSetting 以开启音频流
    m_device->setInterfaceAltSetting(m_inEp.interfaceNum, m_inEp.altSetting);
    m_device->setInterfaceAltSetting(m_outEp.interfaceNum, m_outEp.altSetting);

    // 设置采样率
    m_device->setSampleRate(sampleRate);
    
    // 允许 submitTransfer 执行
    m_running = true;

    // 分配并提交 USB 传输
    if (!allocTransfers()) {
        stop();
        return false;
    }
    
    // 启动轮询线程
    m_workerThread = thread(&UsbAudioStream::eventThreadFunc, this);

    // 设置高优先级
    SetThreadPriority(m_workerThread.native_handle(), THREAD_PRIORITY_TIME_CRITICAL);

    return true;
}

void UsbAudioStream::stop() {
    if (!m_running) return;
    
    m_running = false;
    
    // 唤醒假死的 wait
    SetEvent(m_bufferEvent);
    
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    // 关闭音频流 (恢复到 AltSetting 0)
    if (m_device && m_device->isOpen()) {
        m_device->setInterfaceAltSetting(m_inEp.interfaceNum, 0);
        m_device->setInterfaceAltSetting(m_outEp.interfaceNum, 0);
    }

    freeTransfers();
}

void UsbAudioStream::eventThreadFunc() {
    // This thread constantly polls libusb events driving the isochronous callbacks
    libusb_context* ctx = nullptr; // use default context or store in device
    
    timeval tv = { 0, 1000 }; // 1ms timeout
    while (m_running) {
        int rc = libusb_handle_events_timeout_completed(ctx, &tv, nullptr);
        if (rc < 0 && rc != LIBUSB_ERROR_INTERRUPTED) {
            LOG_ERROR("libusb_handle_events failed: %s", libusb_error_name(rc));
        }
    }
}

bool UsbAudioStream::allocTransfers() {
    // 分配 IN 传输
    int inPacketSize = m_inEp.maxPacketSize;
    for (int i = 0; i < NUM_ISO_TRANSFERS; i++) {
        auto* ctx = new TransferContext();
        ctx->stream = this;
        ctx->buffer = new uint8_t[inPacketSize * PACKETS_PER_TRANSFER];
        ctx->transfer = libusb_alloc_transfer(PACKETS_PER_TRANSFER);
        
        libusb_fill_iso_transfer(ctx->transfer, m_device->getHandle(), m_inEp.address,
            ctx->buffer, inPacketSize * PACKETS_PER_TRANSFER, PACKETS_PER_TRANSFER,
            inCallback, ctx, 0);
        libusb_set_iso_packet_lengths(ctx->transfer, inPacketSize);
        
        m_inTransfers.push_back(ctx);
        if (!submitTransfer(ctx->transfer)) return false;
    }

    // 分配 OUT 传输
    int outPacketSize = m_outEp.maxPacketSize;
    for (int i = 0; i < NUM_ISO_TRANSFERS; i++) {
        auto* ctx = new TransferContext();
        ctx->stream = this;
        ctx->buffer = new uint8_t[outPacketSize * PACKETS_PER_TRANSFER];
        memset(ctx->buffer, 0, outPacketSize * PACKETS_PER_TRANSFER); // Silence
        ctx->transfer = libusb_alloc_transfer(PACKETS_PER_TRANSFER);
        
        libusb_fill_iso_transfer(ctx->transfer, m_device->getHandle(), m_outEp.address,
            ctx->buffer, outPacketSize * PACKETS_PER_TRANSFER, PACKETS_PER_TRANSFER,
            outCallback, ctx, 0);
        libusb_set_iso_packet_lengths(ctx->transfer, outPacketSize);
        
        m_outTransfers.push_back(ctx);
        if (!submitTransfer(ctx->transfer)) return false;
    }

    return true;
}

void UsbAudioStream::freeTransfers() {
    for (auto* ctx : m_inTransfers) {
        if (ctx) {
            libusb_cancel_transfer(ctx->transfer);
            libusb_free_transfer(ctx->transfer);
            delete[] ctx->buffer;
            delete ctx;
        }
    }
    m_inTransfers.clear();

    for (auto* ctx : m_outTransfers) {
        if (ctx) {
            libusb_cancel_transfer(ctx->transfer);
            libusb_free_transfer(ctx->transfer);
            delete[] ctx->buffer;
            delete ctx;
        }
    }
    m_outTransfers.clear();
}

bool UsbAudioStream::submitTransfer(libusb_transfer* transfer) {
    if (!m_running) return false;
    int rc = libusb_submit_transfer(transfer);
    if (rc < 0) {
        LOG_ERROR("libusb_submit_transfer failed: %s", libusb_error_name(rc));
        return false;
    }
    return true;
}

// ============================================================================
// Callbacks
// ============================================================================

void LIBUSB_CALL UsbAudioStream::inCallback(struct libusb_transfer* transfer) {
    auto* ctx = static_cast<TransferContext*>(transfer->user_data);
    if (ctx && ctx->stream->m_running) {
        ctx->stream->handleInTransfer(transfer);
        ctx->stream->submitTransfer(transfer);
    }
}

void LIBUSB_CALL UsbAudioStream::outCallback(struct libusb_transfer* transfer) {
    auto* ctx = static_cast<TransferContext*>(transfer->user_data);
    if (ctx && ctx->stream->m_running) {
        ctx->stream->handleOutTransfer(transfer);
        ctx->stream->submitTransfer(transfer);
    }
}

void UsbAudioStream::handleInTransfer(struct libusb_transfer* transfer) {
    // 处理收到的 USB IN 音频包
    for (int i = 0; i < transfer->num_iso_packets; i++) {
        struct libusb_iso_packet_descriptor* packet = &transfer->iso_packet_desc[i];
        if (packet->status == LIBUSB_TRANSFER_COMPLETED && packet->actual_length > 0) {
            uint8_t* payload = libusb_get_iso_packet_buffer_simple(transfer, i);
            decodePayload(payload, packet->actual_length);
        }
    }
}

void UsbAudioStream::handleOutTransfer(struct libusb_transfer* transfer) {
    // 构造发送到 USB OUT 的音频包
    for (int i = 0; i < transfer->num_iso_packets; i++) {
        struct libusb_iso_packet_descriptor* packet = &transfer->iso_packet_desc[i];
        
        // 通常根据频率，计算当前 microframe 的样本数 (例如 6)
        int numSamples = 6; 
        int frameSize = m_outEp.numChannels * m_outEp.subslotSize;
        int payloadLen = numSamples * frameSize;
        
        uint8_t* payload = libusb_get_iso_packet_buffer_simple(transfer, i);
        encodePayload(payload, payloadLen);
        
        packet->length = payloadLen;
    }
}

// ============================================================================
// 格式转换器: USB 交织 <-> ASIO 非交织
// ============================================================================

void UsbAudioStream::decodePayload(const uint8_t* payload, int payloadLen) {
    if (!m_asioBuffer) return;
    
    int bytesPerSample = m_inEp.subslotSize;
    if (bytesPerSample == 0) bytesPerSample = 4; // Default if not parsed
    int frameSize = m_inEp.numChannels * bytesPerSample;
    int numSamples = payloadLen / frameSize;
    
    for (int s = 0; s < numSamples; s++) {
        for (int ch = 0; ch < m_inEp.numChannels; ch++) {
            float val = 0.0f;
            const uint8_t* data = payload + s * frameSize + ch * bytesPerSample;
            
            if (bytesPerSample == 3) {
                 int32_t valInt = (data[0] | (data[1] << 8) | (data[2] << 16));
                 if (valInt & 0x800000) valInt |= 0xFF000000;
                 val = valInt / 8388608.0f;
            } else if (bytesPerSample == 4) {
                 int32_t valInt = (data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
                 val = valInt / 2147483648.0f;
            }
            
            float* dest = (float*)m_asioBuffer->getBuffer(true, ch, m_currentAsioIndex);
            if (dest && m_samplesCaptured < m_bufferSizeFrames) {
                dest[m_samplesCaptured] = val;
            }
        }
        
        m_samplesCaptured++;
        
        if (m_samplesCaptured >= m_bufferSizeFrames) {
            m_samplesCaptured = 0;
            // 我们在每次蓄满缓冲时发送事件，通知 ASIO 换池
            SetEvent(m_bufferEvent);
        }
    }
}

void UsbAudioStream::encodePayload(uint8_t* payload, int targetLen) {
    if (!m_asioBuffer) {
        memset(payload, 0, targetLen); 
        return;
    }
    
    int bytesPerSample = m_outEp.subslotSize;
    if (bytesPerSample == 0) bytesPerSample = 3; 
    int frameSize = m_outEp.numChannels * bytesPerSample;
    int numSamples = targetLen / frameSize;
    
    for (int s = 0; s < numSamples; s++) {
        if (m_samplesRendered >= m_bufferSizeFrames) {
            m_samplesRendered = 0;
        }
        
        for (int ch = 0; ch < m_outEp.numChannels; ch++) {
            float* src = (float*)m_asioBuffer->getBuffer(false, ch, m_currentAsioIndex);
            float val = 0.0f;
            if (src && m_samplesRendered < m_bufferSizeFrames) {
                val = src[m_samplesRendered];
            }
            
            // 安全裁剪
            if (val > 1.0f) val = 1.0f;
            if (val < -1.0f) val = -1.0f;
            
            uint8_t* data = payload + s * frameSize + ch * bytesPerSample;
            if (bytesPerSample == 3) {
                 int32_t valInt = (int32_t)(val * 8388607.0f);
                 data[0] = (valInt & 0xFF);
                 data[1] = ((valInt >> 8) & 0xFF);
                 data[2] = ((valInt >> 16) & 0xFF);
            } else if (bytesPerSample == 4) {
                 int32_t valInt = (int32_t)(val * 2147483647.0f);
                 data[0] = (valInt & 0xFF);
                 data[1] = ((valInt >> 8) & 0xFF);
                 data[2] = ((valInt >> 16) & 0xFF);
                 data[3] = ((valInt >> 24) & 0xFF);
            }
        }
        m_samplesRendered++;
    }
}

// ============================================================================
// 同步原语 (供 ASIO 回调使用)
// ============================================================================

bool UsbAudioStream::waitForBuffer(int* nextIndex) {
    DWORD r = WaitForSingleObject(m_bufferEvent, 1000);
    if (r == WAIT_OBJECT_0) {
        if (!m_running) return false;
        *nextIndex = m_currentAsioIndex;
        return true;
    }
    return false;
}

void UsbAudioStream::ackBuffer() {
    // ASIO 回调结束，切换双缓冲
    m_currentAsioIndex = 1 - m_currentAsioIndex;
}
