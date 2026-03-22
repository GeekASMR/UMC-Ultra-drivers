/*
 * KsAudioStream.h - Kernel Streaming audio I/O
 * 
 * Directly streams PCM audio through the official UMC kernel driver's 
 * KS filter device, bypassing WASAPI entirely.
 */

#pragma once

#include <windows.h>
#include <ks.h>
#include <ksmedia.h>
#include <setupapi.h>
#include <string>
#include <vector>

class KsAudioStream {
public:
    KsAudioStream();
    ~KsAudioStream();

    // Find and open a KS filter device for Behringer (VID 0x1397)
    // direction: true = capture (pcm_in), false = render (pcm_out)
    bool open(bool capture, int preferredChannels, int sampleRate);
    
    // Create a streaming pin on the filter
    bool createPin(int channels, int bitsPerSample, int sampleRate);
    
    // Set pin state
    bool setState(KSSTATE state);
    
    // Read audio data from capture pin (returns frames read)
    int read(float* buffer, int frames, int channels);
    
    // Write audio data to render pin (returns frames written)
    int write(const float* buffer, int frames, int channels);
    
    // Close and cleanup
    void close();
    
    bool isOpen() const { return m_filterHandle != INVALID_HANDLE_VALUE; }
    bool isPinCreated() const { return m_pinHandle != INVALID_HANDLE_VALUE; }
    int getChannelCount() const { return m_channels; }
    const std::wstring& getDevicePath() const { return m_devicePath; }

private:
    // Find best matching device path
    std::wstring findDevice(bool capture, int preferredChannels, int sampleRate);
    
    // Parse channel count from device path (e.g. "c_10" -> 10)
    static int parseChannelCount(const std::wstring& path);
    
    HANDLE m_filterHandle;
    HANDLE m_pinHandle;
    std::wstring m_devicePath;
    int m_channels;
    int m_sampleRate;
    int m_bitsPerSample;
    bool m_isCapture;
    
    // Intermediate buffer for format conversion (24-bit int <-> float)
    std::vector<unsigned char> m_convBuffer;
};
