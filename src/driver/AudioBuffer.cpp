/*
 * AudioBuffer - 音频缓冲区管理实现
 */

#include "AudioBuffer.h"
#include "../utils/Logger.h"

#define LOG_MODULE "AudioBuffer"

AudioBuffer::AudioBuffer()
    : m_bufferSize(0)
    , m_sampleType(ASIOSTFloat32LSB)
    , m_numInputChannels(0)
    , m_numOutputChannels(0)
    , m_created(false)
{
}

AudioBuffer::~AudioBuffer() {
    dispose();
}

bool AudioBuffer::create(int numInputChannels, int numOutputChannels, 
                          long bufferSize, ASIOSampleType sampleType) {
    LOG_INFO(LOG_MODULE, "Creating buffers: %d in, %d out, size=%ld, type=%d",
             numInputChannels, numOutputChannels, bufferSize, sampleType);

    // Clean up any existing buffers
    dispose();

    m_bufferSize = bufferSize;
    m_sampleType = sampleType;
    m_numInputChannels = numInputChannels;
    m_numOutputChannels = numOutputChannels;

    // Allocate input channel buffers
    m_inputBuffers.resize(numInputChannels);
    for (int i = 0; i < numInputChannels; i++) {
        if (!allocateChannelBuffer(m_inputBuffers[i], bufferSize)) {
            LOG_ERROR(LOG_MODULE, "Failed to allocate input buffer for channel %d", i);
            dispose();
            return false;
        }
    }

    // Allocate output channel buffers
    m_outputBuffers.resize(numOutputChannels);
    for (int i = 0; i < numOutputChannels; i++) {
        if (!allocateChannelBuffer(m_outputBuffers[i], bufferSize)) {
            LOG_ERROR(LOG_MODULE, "Failed to allocate output buffer for channel %d", i);
            dispose();
            return false;
        }
    }

    // Clear all buffers
    clearAll();

    m_created = true;
    LOG_INFO(LOG_MODULE, "Buffers created successfully (%d bytes per sample)",
             getBytesPerSample());
    return true;
}

void AudioBuffer::dispose() {
    for (auto& cb : m_inputBuffers) {
        freeChannelBuffer(cb);
    }
    m_inputBuffers.clear();

    for (auto& cb : m_outputBuffers) {
        freeChannelBuffer(cb);
    }
    m_outputBuffers.clear();

    m_bufferSize = 0;
    m_numInputChannels = 0;
    m_numOutputChannels = 0;
    m_created = false;

    LOG_DEBUG(LOG_MODULE, "Buffers disposed");
}

void* AudioBuffer::getBuffer(bool isInput, int channel, int bufferIndex) const {
    if (bufferIndex < 0 || bufferIndex > 1) return nullptr;

    if (isInput) {
        if (channel < 0 || channel >= m_numInputChannels) return nullptr;
        return m_inputBuffers[channel].buffers[bufferIndex];
    } else {
        if (channel < 0 || channel >= m_numOutputChannels) return nullptr;
        return m_outputBuffers[channel].buffers[bufferIndex];
    }
}

int AudioBuffer::getBytesPerSample() const {
    switch (m_sampleType) {
        case ASIOSTInt16LSB:
        case ASIOSTInt16MSB:
            return 2;
        case ASIOSTInt24LSB:
        case ASIOSTInt24MSB:
            return 3;
        case ASIOSTInt32LSB:
        case ASIOSTInt32MSB:
        case ASIOSTFloat32LSB:
        case ASIOSTFloat32MSB:
        case ASIOSTInt32LSB16:
        case ASIOSTInt32LSB18:
        case ASIOSTInt32LSB20:
        case ASIOSTInt32LSB24:
            return 4;
        case ASIOSTFloat64LSB:
        case ASIOSTFloat64MSB:
            return 8;
        default:
            return 4;
    }
}

void AudioBuffer::clearBuffer(bool isInput, int channel, int bufferIndex) {
    void* buf = getBuffer(isInput, channel, bufferIndex);
    if (buf) {
        memset(buf, 0, m_bufferSize * getBytesPerSample());
    }
}

void AudioBuffer::clearAll() {
    for (int i = 0; i < m_numInputChannels; i++) {
        clearBuffer(true, i, 0);
        clearBuffer(true, i, 1);
    }
    for (int i = 0; i < m_numOutputChannels; i++) {
        clearBuffer(false, i, 0);
        clearBuffer(false, i, 1);
    }
}

bool AudioBuffer::allocateChannelBuffer(ChannelBuffer& cb, long bufferSize) {
    int bytesPerSample = getBytesPerSample();
    size_t allocSize = (size_t)bufferSize * bytesPerSample;

    // Use VirtualAlloc for aligned memory in hot path
    cb.buffers[0] = VirtualAlloc(nullptr, allocSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    cb.buffers[1] = VirtualAlloc(nullptr, allocSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (!cb.buffers[0] || !cb.buffers[1]) {
        freeChannelBuffer(cb);
        return false;
    }

    return true;
}

void AudioBuffer::freeChannelBuffer(ChannelBuffer& cb) {
    if (cb.buffers[0]) {
        VirtualFree(cb.buffers[0], 0, MEM_RELEASE);
        cb.buffers[0] = nullptr;
    }
    if (cb.buffers[1]) {
        VirtualFree(cb.buffers[1], 0, MEM_RELEASE);
        cb.buffers[1] = nullptr;
    }
}

//-------------------------------------------------------------------
// Sample Format Conversion
//-------------------------------------------------------------------

void AudioBuffer::convertInt16ToFloat32(const short* src, float* dst, long numSamples) {
    const float scale = 1.0f / 32768.0f;
    for (long i = 0; i < numSamples; i++) {
        dst[i] = src[i] * scale;
    }
}

void AudioBuffer::convertInt24ToFloat32(const unsigned char* src, float* dst, long numSamples) {
    const float scale = 1.0f / 8388608.0f;
    for (long i = 0; i < numSamples; i++) {
        int value = (src[i * 3] | (src[i * 3 + 1] << 8) | (src[i * 3 + 2] << 16));
        // Sign extend
        if (value & 0x800000) value |= 0xFF000000;
        dst[i] = value * scale;
    }
}

void AudioBuffer::convertInt32ToFloat32(const int* src, float* dst, long numSamples) {
    const float scale = 1.0f / 2147483648.0f;
    for (long i = 0; i < numSamples; i++) {
        dst[i] = src[i] * scale;
    }
}

void AudioBuffer::convertFloat32ToInt16(const float* src, short* dst, long numSamples) {
    for (long i = 0; i < numSamples; i++) {
        float val = src[i];
        if (val > 1.0f) val = 1.0f;
        if (val < -1.0f) val = -1.0f;
        dst[i] = (short)(val * 32767.0f);
    }
}

void AudioBuffer::convertFloat32ToInt24(const float* src, unsigned char* dst, long numSamples) {
    for (long i = 0; i < numSamples; i++) {
        float val = src[i];
        if (val > 1.0f) val = 1.0f;
        if (val < -1.0f) val = -1.0f;
        int intVal = (int)(val * 8388607.0f);
        dst[i * 3]     = (unsigned char)(intVal & 0xFF);
        dst[i * 3 + 1] = (unsigned char)((intVal >> 8) & 0xFF);
        dst[i * 3 + 2] = (unsigned char)((intVal >> 16) & 0xFF);
    }
}

void AudioBuffer::convertFloat32ToInt32(const float* src, int* dst, long numSamples) {
    for (long i = 0; i < numSamples; i++) {
        float val = src[i];
        if (val > 1.0f) val = 1.0f;
        if (val < -1.0f) val = -1.0f;
        dst[i] = (int)(val * 2147483647.0f);
    }
}

void AudioBuffer::mixFloat32ToInt32(const float* src, int* dst, long numSamples) {
    for (long i = 0; i < numSamples; i++) {
        float val = src[i];
        if (val == 0.0f) continue;
        
        long long existing = dst[i];
        long long added = (long long)(val * 2147483647.0f);
        long long sum = existing + added;
        
        if (sum > 2147483647LL) sum = 2147483647LL;
        if (sum < -2147483648LL) sum = -2147483648LL;
        
        dst[i] = (int)sum;
    }
}

void AudioBuffer::copyWasapiToInput(const BYTE* wasapiData, int wasapiChannels,
                                     int wasapiBytesPerSample, int bufferIndex,
                                     int channelOffset, int numChannels,
                                     int numFrames) {
    if (!m_created || !wasapiData) return;

    // Use actual WASAPI frame count, capped to our buffer size
    long framesToCopy = (numFrames >= 0) ? (long)(std::min)((long)numFrames, m_bufferSize) : m_bufferSize;
    int bytesPerFrame = wasapiChannels * wasapiBytesPerSample;

    for (int ch = 0; ch < numChannels && (ch + channelOffset) < m_numInputChannels; ch++) {
        float* dstBuffer = (float*)getBuffer(true, ch + channelOffset, bufferIndex);
        if (!dstBuffer) continue;

        // Convert only the valid frames from WASAPI
        for (long s = 0; s < framesToCopy; s++) {
            const BYTE* sampleData = wasapiData + s * bytesPerFrame + ch * wasapiBytesPerSample;

            switch (wasapiBytesPerSample) {
                case 2: {
                    short value = *(const short*)sampleData;
                    dstBuffer[s] = value / 32768.0f;
                    break;
                }
                case 3: {
                    int value = sampleData[0] | (sampleData[1] << 8) | (sampleData[2] << 16);
                    if (value & 0x800000) value |= 0xFF000000;
                    dstBuffer[s] = value / 8388608.0f;
                    break;
                }
                case 4: {
                    if (m_sampleType == ASIOSTFloat32LSB) {
                        dstBuffer[s] = *(const float*)sampleData;
                    } else {
                        int value = *(const int*)sampleData;
                        dstBuffer[s] = value / 2147483648.0f;
                    }
                    break;
                }
            }
        }

        // Zero-fill the remainder if WASAPI gave fewer frames than ASIO buffer size
        if (framesToCopy < m_bufferSize) {
            memset(&dstBuffer[framesToCopy], 0, (m_bufferSize - framesToCopy) * sizeof(float));
        }
    }
}

void AudioBuffer::copyOutputToWasapi(BYTE* wasapiData, int wasapiChannels,
                                      int wasapiBytesPerSample, int bufferIndex,
                                      int channelOffset, int numChannels,
                                      int numFrames) {
    if (!m_created || !wasapiData) return;

    // Use actual available frames, capped to our buffer size
    long framesToCopy = (numFrames >= 0) ? (long)(std::min)((long)numFrames, m_bufferSize) : m_bufferSize;
    int bytesPerFrame = wasapiChannels * wasapiBytesPerSample;

    // Clear the entire WASAPI output buffer first (use numFrames, not m_bufferSize!)
    memset(wasapiData, 0, framesToCopy * bytesPerFrame);

    for (int ch = 0; ch < numChannels && (ch + channelOffset) < m_numOutputChannels; ch++) {
        const float* srcBuffer = (const float*)getBuffer(false, ch + channelOffset, bufferIndex);
        if (!srcBuffer) continue;

        for (long s = 0; s < framesToCopy; s++) {
            BYTE* sampleData = wasapiData + s * bytesPerFrame + ch * wasapiBytesPerSample;
            float value = srcBuffer[s];

            // Clamp
            if (value > 1.0f) value = 1.0f;
            if (value < -1.0f) value = -1.0f;

            switch (wasapiBytesPerSample) {
                case 2: {
                    *(short*)sampleData = (short)(value * 32767.0f);
                    break;
                }
                case 3: {
                    int intVal = (int)(value * 8388607.0f);
                    sampleData[0] = (BYTE)(intVal & 0xFF);
                    sampleData[1] = (BYTE)((intVal >> 8) & 0xFF);
                    sampleData[2] = (BYTE)((intVal >> 16) & 0xFF);
                    break;
                }
                case 4: {
                    if (m_sampleType == ASIOSTFloat32LSB) {
                        *(float*)sampleData = value;
                    } else {
                        *(int*)sampleData = (int)(value * 2147483647.0f);
                    }
                    break;
                }
            }
        }
    }
}
