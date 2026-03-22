/*
 * DeviceDetector - 百灵达设备检测实现
 */

#include "DeviceDetector.h"
#include "../utils/Logger.h"
#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>
#include <algorithm>
#include <locale>
#include <codecvt>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "ole32.lib")

#define LOG_MODULE "DeviceDetector"

// Helper: Convert wide string to lowercase
static std::wstring toLower(const std::wstring& str) {
    std::wstring result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::towlower);
    return result;
}

// Helper: Wide string to UTF-8
static std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.length(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.length(), &result[0], size, nullptr, nullptr);
    return result;
}

DeviceDetector::DeviceDetector() {
    LOG_DEBUG(LOG_MODULE, "DeviceDetector created");
}

DeviceDetector::~DeviceDetector() {
    m_devices.clear();
}

bool DeviceDetector::enumerate() {
    LOG_INFO(LOG_MODULE, "Starting device enumeration...");
    m_devices.clear();

    HRESULT hr;
    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDeviceCollection* pCollection = nullptr;
    IMMDevice* pDefaultDevice = nullptr;
    std::wstring defaultDeviceId;

    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)&pEnumerator);

    if (FAILED(hr)) {
        LOG_ERROR(LOG_MODULE, "Failed to create device enumerator: 0x%08X", hr);
        return false;
    }

    // Get default device ID for reference
    hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDefaultDevice);
    if (SUCCEEDED(hr) && pDefaultDevice) {
        LPWSTR pwszId = nullptr;
        pDefaultDevice->GetId(&pwszId);
        if (pwszId) {
            defaultDeviceId = pwszId;
            CoTaskMemFree(pwszId);
        }
        pDefaultDevice->Release();
    }

    // Enumerate both capture and render devices
    EDataFlow flows[] = { eCapture, eRender };
    const char* flowNames[] = { "Capture", "Render" };

    for (int f = 0; f < 2; f++) {
        hr = pEnumerator->EnumAudioEndpoints(flows[f], DEVICE_STATE_ACTIVE, &pCollection);
        if (FAILED(hr)) {
            LOG_WARN(LOG_MODULE, "Failed to enumerate %s devices: 0x%08X", flowNames[f], hr);
            continue;
        }

        UINT count = 0;
        pCollection->GetCount(&count);
        LOG_DEBUG(LOG_MODULE, "Found %d active %s devices", count, flowNames[f]);

        for (UINT i = 0; i < count; i++) {
            IMMDevice* pDevice = nullptr;
            hr = pCollection->Item(i, &pDevice);
            if (FAILED(hr)) continue;

            // Get device ID
            LPWSTR pwszId = nullptr;
            pDevice->GetId(&pwszId);
            std::wstring deviceId = pwszId ? pwszId : L"";
            if (pwszId) CoTaskMemFree(pwszId);

            // Get friendly name
            IPropertyStore* pProps = nullptr;
            pDevice->OpenPropertyStore(STGM_READ, &pProps);
            
            std::wstring friendlyName;
            if (pProps) {
                PROPVARIANT varName;
                PropVariantInit(&varName);
                hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
                if (SUCCEEDED(hr) && varName.vt == VT_LPWSTR) {
                    friendlyName = varName.pwszVal;
                }
                PropVariantClear(&varName);
                pProps->Release();
            }

            LOG_DEBUG(LOG_MODULE, "  Device[%d]: %s", i, wideToUtf8(friendlyName).c_str());

            // Check if it's a Behringer device
            if (isBehringerDevice(friendlyName) || isBehringerDevice(deviceId)) {
                BehringerDevice bDevice;
                bDevice.deviceId = deviceId;
                bDevice.friendlyName = friendlyName;
                bDevice.isDefault = (deviceId == defaultDeviceId);

                if (identifyModel(friendlyName, deviceId, bDevice)) {
                    setModelCapabilities(bDevice);
                    
                    // Only add capture devices to the main list (we'll find render matches later)
                    if (flows[f] == eCapture) {
                        m_devices.push_back(bDevice);
                        LOG_INFO(LOG_MODULE, "  -> Detected Behringer %s (In:%d Out:%d MaxRate:%d)",
                                 bDevice.modelName.c_str(),
                                 bDevice.inputChannels,
                                 bDevice.outputChannels,
                                 bDevice.maxSampleRate);
                    }
                }
            }

            pDevice->Release();
        }

        pCollection->Release();
    }

    pEnumerator->Release();

    // If no capture devices found, try to match any Behringer render device
    if (m_devices.empty()) {
        LOG_INFO(LOG_MODULE, "No Behringer capture devices found, checking render devices...");
        
        // Re-enumerate render devices
        hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr,
            CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
            (void**)&pEnumerator);
        
        if (SUCCEEDED(hr)) {
            hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
            if (SUCCEEDED(hr)) {
                UINT count = 0;
                pCollection->GetCount(&count);
                
                for (UINT i = 0; i < count; i++) {
                    IMMDevice* pDevice = nullptr;
                    pCollection->Item(i, &pDevice);
                    if (!pDevice) continue;

                    LPWSTR pwszId = nullptr;
                    pDevice->GetId(&pwszId);
                    std::wstring deviceId = pwszId ? pwszId : L"";
                    if (pwszId) CoTaskMemFree(pwszId);

                    IPropertyStore* pProps = nullptr;
                    pDevice->OpenPropertyStore(STGM_READ, &pProps);
                    
                    std::wstring friendlyName;
                    if (pProps) {
                        PROPVARIANT varName;
                        PropVariantInit(&varName);
                        pProps->GetValue(PKEY_Device_FriendlyName, &varName);
                        if (varName.vt == VT_LPWSTR)
                            friendlyName = varName.pwszVal;
                        PropVariantClear(&varName);
                        pProps->Release();
                    }

                    if (isBehringerDevice(friendlyName) || isBehringerDevice(deviceId)) {
                        BehringerDevice bDevice;
                        bDevice.deviceId = deviceId;
                        bDevice.friendlyName = friendlyName;
                        bDevice.isDefault = false;

                        if (identifyModel(friendlyName, deviceId, bDevice)) {
                            setModelCapabilities(bDevice);
                            m_devices.push_back(bDevice);
                            LOG_INFO(LOG_MODULE, "  -> Detected Behringer %s (render only)",
                                     bDevice.modelName.c_str());
                        }
                    }

                    pDevice->Release();
                }
                pCollection->Release();
            }
            pEnumerator->Release();
        }
    }

    LOG_INFO(LOG_MODULE, "Device enumeration complete. Found %d Behringer device(s)", (int)m_devices.size());
    return !m_devices.empty();
}

const BehringerDevice* DeviceDetector::getDevice(int index) const {
    if (index >= 0 && index < (int)m_devices.size())
        return &m_devices[index];
    return nullptr;
}

const BehringerDevice* DeviceDetector::getPreferredDevice() const {
    // First try to find the default device
    for (const auto& dev : m_devices) {
        if (dev.isDefault) return &dev;
    }
    // Otherwise return the first device
    if (!m_devices.empty())
        return &m_devices[0];
    return nullptr;
}

const BehringerDevice* DeviceDetector::findByDeviceId(const std::wstring& deviceId) const {
    for (const auto& dev : m_devices) {
        if (dev.deviceId == deviceId) return &dev;
    }
    return nullptr;
}

std::wstring DeviceDetector::findMatchingRenderDevice(const BehringerDevice& captureDevice) const {
    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDeviceCollection* pCollection = nullptr;

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)&pEnumerator);

    if (FAILED(hr)) return L"";

    hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr)) {
        pEnumerator->Release();
        return L"";
    }

    std::wstring result;
    UINT count = 0;
    pCollection->GetCount(&count);

    for (UINT i = 0; i < count; i++) {
        IMMDevice* pDevice = nullptr;
        pCollection->Item(i, &pDevice);
        if (!pDevice) continue;

        IPropertyStore* pProps = nullptr;
        pDevice->OpenPropertyStore(STGM_READ, &pProps);

        if (pProps) {
            PROPVARIANT varName;
            PropVariantInit(&varName);
            pProps->GetValue(PKEY_Device_FriendlyName, &varName);
            
            if (varName.vt == VT_LPWSTR) {
                std::wstring name = varName.pwszVal;
                if (isBehringerDevice(name)) {
                    LPWSTR pwszId = nullptr;
                    pDevice->GetId(&pwszId);
                    if (pwszId) {
                        result = pwszId;
                        CoTaskMemFree(pwszId);
                    }
                    PropVariantClear(&varName);
                    pProps->Release();
                    pDevice->Release();
                    break;
                }
            }
            PropVariantClear(&varName);
            pProps->Release();
        }

        pDevice->Release();
    }

    pCollection->Release();
    pEnumerator->Release();

    return result;
}

bool DeviceDetector::identifyModel(const std::wstring& deviceName, const std::wstring& deviceId, 
                                    BehringerDevice& device) {
    std::wstring nameLower = toLower(deviceName);
    std::wstring idLower = toLower(deviceId);

    // Match order: most specific first to avoid false matches
    // UMC 1820
    if (nameLower.find(L"umc1820") != std::wstring::npos || 
        nameLower.find(L"umc 1820") != std::wstring::npos ||
        idLower.find(L"pid_0503") != std::wstring::npos ||
        idLower.find(L"pid_0514") != std::wstring::npos) {
        device.modelName = "UMC1820";
        return true;
    }
    // UMC 404 series
    if (nameLower.find(L"umc404") != std::wstring::npos || 
        nameLower.find(L"umc 404") != std::wstring::npos ||
        idLower.find(L"pid_0502") != std::wstring::npos ||
        idLower.find(L"pid_0506") != std::wstring::npos ||
        idLower.find(L"pid_0509") != std::wstring::npos) {
        // Distinguish HD vs non-HD
        if (nameLower.find(L"hd") != std::wstring::npos ||
            idLower.find(L"pid_0509") != std::wstring::npos) {
            device.modelName = "UMC404HD";
        } else if (nameLower.find(L"192") != std::wstring::npos ||
                   idLower.find(L"pid_0506") != std::wstring::npos) {
            device.modelName = "UMC404_192k";
        } else {
            device.modelName = "UMC404_96k";
        }
        return true;
    }
    // UMC 204 series
    if (nameLower.find(L"umc204") != std::wstring::npos || 
        nameLower.find(L"umc 204") != std::wstring::npos ||
        idLower.find(L"pid_0501") != std::wstring::npos ||
        idLower.find(L"pid_0505") != std::wstring::npos ||
        idLower.find(L"pid_0508") != std::wstring::npos) {
        if (nameLower.find(L"hd") != std::wstring::npos ||
            idLower.find(L"pid_0508") != std::wstring::npos) {
            device.modelName = "UMC204HD";
        } else if (nameLower.find(L"192") != std::wstring::npos ||
                   idLower.find(L"pid_0505") != std::wstring::npos) {
            device.modelName = "UMC204_192k";
        } else {
            device.modelName = "UMC204_96k";
        }
        return true;
    }
    // UMC 202 series
    if (nameLower.find(L"umc202") != std::wstring::npos || 
        nameLower.find(L"umc 202") != std::wstring::npos ||
        idLower.find(L"pid_0500") != std::wstring::npos ||
        idLower.find(L"pid_0504") != std::wstring::npos ||
        idLower.find(L"pid_0507") != std::wstring::npos) {
        if (nameLower.find(L"hd") != std::wstring::npos ||
            idLower.find(L"pid_0507") != std::wstring::npos) {
            device.modelName = "UMC202HD";
        } else if (nameLower.find(L"192") != std::wstring::npos ||
                   idLower.find(L"pid_0504") != std::wstring::npos) {
            device.modelName = "UMC202_192k";
        } else {
            device.modelName = "UMC202_96k";
        }
        return true;
    }
    // UMC22 (legacy, no specific PID in new INF)
    if (nameLower.find(L"umc22") != std::wstring::npos || 
        nameLower.find(L"umc 22") != std::wstring::npos) {
        device.modelName = "UMC22";
        return true;
    }

    // Generic Behringer device
    if (isBehringerDevice(deviceName) || isBehringerDevice(deviceId)) {
        device.modelName = "UMC (Generic)";
        return true;
    }

    return false;
}

void DeviceDetector::setModelCapabilities(BehringerDevice& device) {
    // Default values
    device.analogInputs = 0;
    device.analogOutputs = 0;
    device.digitalInputs = 0;
    device.digitalOutputs = 0;
    device.hasDigitalIO = false;
    device.digitalMode = DigitalMode::SPDIF;

    if (device.modelName == "UMC22") {
        device.analogInputs = 2;
        device.analogOutputs = 2;
        device.maxSampleRate = 48000;
        device.hasMIDI = false;
    }
    // --- UMC 202 series ---
    else if (device.modelName == "UMC202_96k") {
        device.analogInputs = 2;
        device.analogOutputs = 2;
        device.maxSampleRate = 96000;
        device.hasMIDI = false;
    }
    else if (device.modelName == "UMC202_192k" || device.modelName == "UMC202HD") {
        device.analogInputs = 2;
        device.analogOutputs = 2;
        device.maxSampleRate = 192000;
        device.hasMIDI = false;
    }
    // --- UMC 204 series ---
    else if (device.modelName == "UMC204_96k") {
        device.analogInputs = 2;
        device.analogOutputs = 4;
        device.maxSampleRate = 96000;
        device.hasMIDI = false;
    }
    else if (device.modelName == "UMC204_192k" || device.modelName == "UMC204HD") {
        device.analogInputs = 2;
        device.analogOutputs = 4;
        device.maxSampleRate = 192000;
        device.hasMIDI = false;
    }
    // --- UMC 404 series ---
    else if (device.modelName == "UMC404_96k") {
        device.analogInputs = 4;
        device.analogOutputs = 4;
        device.maxSampleRate = 96000;
        device.hasMIDI = true;
    }
    else if (device.modelName == "UMC404_192k" || device.modelName == "UMC404HD") {
        device.analogInputs = 4;
        device.analogOutputs = 4;
        device.maxSampleRate = 192000;
        device.hasMIDI = true;
    }
    // --- UMC 1820 ---
    else if (device.modelName == "UMC1820") {
        device.analogInputs = 8;
        device.analogOutputs = 10;
        device.hasDigitalIO = true;
        device.digitalMode = DigitalMode::SPDIF;  // Default, physical switch on hardware
        // SPDIF mode: 2 digital in + 2 digital out
        // ADAT mode @48kHz: 8 digital in + 8 digital out
        // ADAT mode @96kHz: 4 digital in + 4 digital out
        device.digitalInputs = 2;   // SPDIF default
        device.digitalOutputs = 2;
        device.maxSampleRate = 96000;
        device.hasMIDI = true;
    }
    else {
        // Generic defaults
        device.analogInputs = 2;
        device.analogOutputs = 2;
        device.maxSampleRate = 192000;
        device.hasMIDI = false;
    }

    // Compute total channel counts
    device.inputChannels = device.analogInputs + device.digitalInputs;
    device.outputChannels = device.analogOutputs + device.digitalOutputs;
}

bool DeviceDetector::isBehringerDevice(const std::wstring& name) const {
    std::wstring lower = toLower(name);
    return (lower.find(L"behringer") != std::wstring::npos ||
            lower.find(L"umc") != std::wstring::npos ||
            lower.find(L"vid_1397") != std::wstring::npos ||
            lower.find(L"\u767e\u7075\u8fbe") != std::wstring::npos);
}
