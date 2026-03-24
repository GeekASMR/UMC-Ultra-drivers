/*
 * AudioBuffer - 音频缓冲区管理
 * 
 * 实现 ASIO 双缓冲机制和采样格式转换
 */

#pragma once

#include <windows.h>
#include "../asio/asio.h"
#include <vector>
#include <cstring>
#include <cmath>

class AudioBuffer {
public:
    AudioBuffer();
    ~AudioBuffer();

    // Initialize buffers for a given channel configuration
    bool create(int numInputChannels, int numOutputChannels, 
                long bufferSize, ASIOSampleType sampleType);

    // Release all buffer memory
    void dispose();

    // Get buffer pointer for a specific channel and buffer index
    void* getBuffer(bool isInput, int channel, int bufferIndex) const;

    // Get the current buffer size
    long getBufferSize() const { return m_bufferSize; }

    // Get the sample type
    ASIOSampleType getSampleType() const { return m_sampleType; }

    // Get bytes per sample for current type
    int getBytesPerSample() const;

    // Clear a specific buffer
    void clearBuffer(bool isInput, int channel, int bufferIndex);

    // Clear all buffers
    void clearAll();

    // Sample format conversion utilities
    static void convertInt16ToFloat32(const short* src, float* dst, long numSamples);
    static void convertInt24ToFloat32(const unsigned char* src, float* dst, long numSamples);
    static void convertInt32ToFloat32(const int* src, float* dst, long numSamples);
    static void convertFloat32ToInt16(const float* src, short* dst, long numSamples);
    static void convertFloat32ToInt24(const float* src, unsigned char* dst, long numSamples);
    static void convertFloat32ToInt32(const float* src, int* dst, long numSamples);
    static void mixFloat32ToInt32(const float* src, int* dst, long numSamples);

    // Copy WASAPI buffer to ASIO input buffer (with format conversion)
    // numFrames: actual number of frames from WASAPI (may differ from m_bufferSize)
    void copyWasapiToInput(const BYTE* wasapiData, int wasapiChannels, 
                           int wasapiBytesPerSample, int bufferIndex,
                           int channelOffset, int numChannels,
                           int numFrames = -1);

    // Copy ASIO output buffer to WASAPI buffer (with format conversion)
    // numFrames: actual number of frames for WASAPI output (may differ from m_bufferSize)
    void copyOutputToWasapi(BYTE* wasapiData, int wasapiChannels,
                            int wasapiBytesPerSample, int bufferIndex,
                            int channelOffset, int numChannels,
                            int numFrames = -1);

    bool isCreated() const { return m_created; }

private:
    struct ChannelBuffer {
        void* buffers[2];  // Double-buffered
    };

    std::vector<ChannelBuffer> m_inputBuffers;
    std::vector<ChannelBuffer> m_outputBuffers;
    
    long m_bufferSize;
    ASIOSampleType m_sampleType;
    int m_numInputChannels;
    int m_numOutputChannels;
    bool m_created;

    // Allocate a single channel's double buffer
    bool allocateChannelBuffer(ChannelBuffer& cb, long bufferSize);
    void freeChannelBuffer(ChannelBuffer& cb);
};
