/*
 * DeviceDetector - 百灵达设备检测
 * 
 * 检测系统中连接的百灵达 UMC 系列 USB 声卡，
 * 通过 WASAPI 设备枚举和 USB VID/PID 匹配。
 */

#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <string>
#include <vector>

// Behringer USB Vendor ID
#define BEHRINGER_USB_VID  0x1397

// Known Behringer UMC Product IDs (from official INF v5.72)
#define BEHRINGER_UMC202_96K_PID    0x0500
#define BEHRINGER_UMC204_96K_PID    0x0501
#define BEHRINGER_UMC404_96K_PID    0x0502
#define BEHRINGER_UMC1820_PID       0x0503
#define BEHRINGER_UMC202_192K_PID   0x0504
#define BEHRINGER_UMC204_192K_PID   0x0505
#define BEHRINGER_UMC404_192K_PID   0x0506
#define BEHRINGER_UMC202HD_PID      0x0507
#define BEHRINGER_UMC204HD_PID      0x0508
#define BEHRINGER_UMC404HD_PID      0x0509
#define BEHRINGER_UMC1820_V2_PID    0x0514

// Digital I/O mode (UMC1820 has physical SPDIF/ADAT switch)
enum class DigitalMode {
    SPDIF,     // 2 channels (coaxial/optical)
    ADAT       // 8 channels @48k, 4 channels @96k (optical)
};

// Device model structure
struct BehringerDevice {
    std::wstring deviceId;       // WASAPI device ID
    std::wstring friendlyName;   // User-friendly name
    std::string  modelName;      // e.g., "UMC202HD"
    int inputChannels;           // Number of hardware input channels
    int outputChannels;          // Number of hardware output channels
    int analogInputs;            // Number of analog input channels
    int analogOutputs;           // Number of analog output channels
    int digitalInputs;           // Number of digital I/O inputs (SPDIF or ADAT)
    int digitalOutputs;          // Number of digital I/O outputs
    int maxSampleRate;           // Maximum sample rate
    bool hasMIDI;                // MIDI support
    bool hasDigitalIO;           // Has SPDIF/ADAT optical I/O
    DigitalMode digitalMode;     // Current digital I/O mode
    bool isDefault;              // Is system default device
};

class DeviceDetector {
public:
    DeviceDetector();
    ~DeviceDetector();

    // Enumerate all Behringer UMC devices
    bool enumerate();

    // Get the list of detected devices
    const std::vector<BehringerDevice>& getDevices() const { return m_devices; }

    // Get a specific device by index
    const BehringerDevice* getDevice(int index) const;

    // Get the preferred (default) device
    const BehringerDevice* getPreferredDevice() const;

    // Find a device by its WASAPI ID
    const BehringerDevice* findByDeviceId(const std::wstring& deviceId) const;

    // Check if any Behringer device is connected
    bool hasDevices() const { return !m_devices.empty(); }

    // Get count of detected devices
    int getDeviceCount() const { return (int)m_devices.size(); }

    // Get the playback (render) device matching the given device
    std::wstring findMatchingRenderDevice(const BehringerDevice& captureDevice) const;

private:
    // Detect model from device name or ID
    bool identifyModel(const std::wstring& deviceName, const std::wstring& deviceId, 
                       BehringerDevice& device);

    // Set device capabilities based on model
    void setModelCapabilities(BehringerDevice& device);

    // Check if a device name indicates a Behringer UMC device
    bool isBehringerDevice(const std::wstring& name) const;

    std::vector<BehringerDevice> m_devices;
};
