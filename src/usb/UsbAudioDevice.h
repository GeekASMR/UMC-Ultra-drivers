/*
 * UsbAudioDevice.h - UAC2 Device Enumeration and Control
 */

#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "libusb.h"

// Common sample rates
const uint32_t DEFAULT_SAMPLE_RATES[] = { 44100, 48000, 88200, 96000, 176400, 192000 };

struct AudioEndpointInfo {
    uint8_t  address;       // EP address (e.g. 0x01, 0x82)
    uint8_t  direction;     // 0 = OUT (Render), 1 = IN (Capture)
    uint16_t maxPacketSize; // Max packet size
    uint8_t  interval;      // Polling interval
    uint8_t  interfaceNum;  // Associated interface
    uint8_t  altSetting;    // AltSetting for audio stream
    uint8_t  syncType;      // Sync type
    uint8_t  feedbackEp;    // Feedback EP

    int      numChannels;   
    int      subslotSize;   
    int      bitResolution; 
};

class UsbAudioDevice {
public:
    UsbAudioDevice();
    ~UsbAudioDevice();

    bool open(uint16_t vid, uint16_t pid);
    void close();
    bool isOpen() const { return m_handle != nullptr; }

    const std::vector<AudioEndpointInfo>& getCaptureEndpoints() const { return m_captureEndpoints; }
    const std::vector<AudioEndpointInfo>& getRenderEndpoints() const { return m_renderEndpoints; }
    
    bool setSampleRate(uint32_t rate);
    uint32_t getSampleRate();
    
    bool setInterfaceAltSetting(uint8_t interfaceNum, uint8_t altSetting);

    libusb_device_handle* getHandle() const { return m_handle; }

private:
    bool parseDescriptors();
    bool querySampleRates();

    libusb_context*       m_ctx;
    libusb_device_handle* m_handle;
    
    std::vector<AudioEndpointInfo> m_captureEndpoints;
    std::vector<AudioEndpointInfo> m_renderEndpoints;
    std::vector<uint8_t> m_clockSourceIds;
    
    uint8_t m_acInterfaceNum;
};
