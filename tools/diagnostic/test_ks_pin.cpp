/*
 * test_ks_pin.cpp - Test creating a KS streaming pin
 * Debug the exact format needed for IOCTL_KS_CREATE_PIN
 */

#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <mmreg.h>
#include <devioctl.h>
#include <ks.h>
#include <ksmedia.h>
#include <stdio.h>
#include <string>

#pragma comment(lib, "setupapi.lib")

#ifndef IOCTL_KS_CREATE_PIN
#define IOCTL_KS_CREATE_PIN CTL_CODE(FILE_DEVICE_KS, 0x003, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif

typedef struct {
    KSDATAFORMAT DataFormat;
    WAVEFORMATEX WaveFormatEx;
} MY_KSDATAFORMAT_WAVEFORMATEX;

// Find the capture device with maximum channels
std::wstring findDevice(const GUID& category, const wchar_t* prefix) {
    HDEVINFO devInfo = SetupDiGetClassDevsW(&category, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return L"";

    SP_DEVICE_INTERFACE_DATA ifData;
    ifData.cbSize = sizeof(ifData);
    std::wstring bestPath;
    int bestCh = 0;

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &category, i, &ifData); i++) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &size, nullptr);
        auto* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)malloc(size);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, size, nullptr, nullptr)) {
            std::wstring path(detail->DevicePath);
            if (path.find(L"vid_1397") != std::wstring::npos && path.find(prefix) != std::wstring::npos) {
                // Parse channel count
                size_t pos = path.find(L"_c_");
                int ch = 0;
                if (pos != std::wstring::npos) {
                    pos += 3;
                    while (pos < path.size() && path[pos] >= L'0' && path[pos] <= L'9')
                        ch = ch * 10 + (path[pos++] - L'0');
                }
                if (ch > bestCh) { bestCh = ch; bestPath = path; }
            }
        }
        free(detail);
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    return bestPath;
}

bool queryPinDataRanges(HANDLE hFilter, ULONG pinId) {
    KSP_PIN pinProp;
    pinProp.Property.Set = KSPROPSETID_Pin;
    pinProp.Property.Id = KSPROPERTY_PIN_DATARANGES;
    pinProp.Property.Flags = KSPROPERTY_TYPE_GET;
    pinProp.PinId = pinId;

    // First get size
    DWORD size = 0;
    DeviceIoControl(hFilter, IOCTL_KS_PROPERTY,
        &pinProp, sizeof(pinProp), nullptr, 0, &size, nullptr);
    
    if (size == 0) {
        // Try with a buffer
        size = 4096;
    }

    printf("    Pin[%d] DATARANGES size = %d\n", pinId, size);
    
    auto* buffer = (BYTE*)malloc(size);
    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl(hFilter, IOCTL_KS_PROPERTY,
        &pinProp, sizeof(pinProp), buffer, size, &bytesReturned, nullptr);
    
    if (!ok) {
        printf("    Pin[%d] DATARANGES query failed: %d\n", pinId, GetLastError());
        free(buffer);
        return false;
    }

    auto* multiItem = (KSMULTIPLE_ITEM*)buffer;
    printf("    Pin[%d] DATARANGES: count=%d, totalSize=%d\n", 
           pinId, multiItem->Count, multiItem->Size);

    BYTE* ptr = buffer + sizeof(KSMULTIPLE_ITEM);
    for (ULONG i = 0; i < multiItem->Count; i++) {
        auto* range = (KSDATARANGE*)ptr;
        
        printf("    Range[%d]: size=%d major:", i, range->FormatSize);
        
        // Check if it's audio  
        if (IsEqualGUID(range->MajorFormat, KSDATAFORMAT_TYPE_AUDIO)) {
            printf("AUDIO ");
            
            if (IsEqualGUID(range->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)) {
                printf("WAVEFORMATEX ");
                auto* audioRange = (KSDATARANGE_AUDIO*)ptr;
                printf("ch=%d bits=%d-%d rate=%d-%d\n",
                    audioRange->MaximumChannels,
                    audioRange->MinimumBitsPerSample, audioRange->MaximumBitsPerSample,
                    audioRange->MinimumSampleFrequency, audioRange->MaximumSampleFrequency);
            } else if (IsEqualGUID(range->Specifier, KSDATAFORMAT_SPECIFIER_DSOUND)) {
                printf("DSOUND\n");
            } else {
                printf("other specifier\n");
            }
        } else if (IsEqualGUID(range->MajorFormat, KSDATAFORMAT_TYPE_WILDCARD)) {
            printf("WILDCARD\n");
        } else {
            printf("other\n");
        }
        
        // Advance to next range (aligned)
        DWORD rangeSize = range->FormatSize;
        if (rangeSize < sizeof(KSDATARANGE)) rangeSize = sizeof(KSDATARANGE);
        rangeSize = (rangeSize + 7) & ~7; // 8-byte align
        ptr += rangeSize;
    }
    
    free(buffer);
    return true;
}

bool tryCreatePin(HANDLE hFilter, ULONG pinId, int channels, int bitsPerSample, int sampleRate) {
    printf("\n  Trying pin %d: %d ch, %d-bit, %d Hz...\n", pinId, channels, bitsPerSample, sampleRate);
    
    // Build WAVEFORMATEXTENSIBLE for better compatibility
    WAVEFORMATEXTENSIBLE wfxe = {};
    wfxe.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfxe.Format.nChannels = (WORD)channels;
    wfxe.Format.nSamplesPerSec = sampleRate;
    wfxe.Format.wBitsPerSample = (WORD)((bitsPerSample + 7) & ~7); // round up to 8/16/24/32
    if (bitsPerSample == 24) wfxe.Format.wBitsPerSample = 32; // 24-bit in 32-bit container
    wfxe.Format.nBlockAlign = wfxe.Format.nChannels * wfxe.Format.wBitsPerSample / 8;
    wfxe.Format.nAvgBytesPerSec = wfxe.Format.nSamplesPerSec * wfxe.Format.nBlockAlign;
    wfxe.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wfxe.Samples.wValidBitsPerSample = (WORD)bitsPerSample;
    wfxe.dwChannelMask = (channels == 2) ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : 0;
    wfxe.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

    // KSPIN_CONNECT + KSDATAFORMAT + WAVEFORMATEXTENSIBLE
    DWORD connectSize = sizeof(KSPIN_CONNECT) + sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEXTENSIBLE);
    auto* connect = (KSPIN_CONNECT*)calloc(1, connectSize);

    connect->PinId = pinId;
    connect->PinToHandle = nullptr;
    connect->Priority.PriorityClass = KSPRIORITY_NORMAL;
    connect->Priority.PrioritySubClass = 1;
    connect->Interface.Set = KSINTERFACESETID_Standard;
    connect->Interface.Id = KSINTERFACE_STANDARD_STREAMING;
    connect->Medium.Set = KSMEDIUMSETID_Standard;
    connect->Medium.Id = KSMEDIUM_TYPE_ANYINSTANCE;

    auto* dataFormat = (KSDATAFORMAT*)(connect + 1);
    dataFormat->MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
    dataFormat->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    dataFormat->Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
    dataFormat->FormatSize = sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEXTENSIBLE);
    dataFormat->SampleSize = wfxe.Format.nBlockAlign;

    memcpy(dataFormat + 1, &wfxe, sizeof(WAVEFORMATEXTENSIBLE));

    HANDLE pinHandle = INVALID_HANDLE_VALUE;
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(hFilter,
        IOCTL_KS_CREATE_PIN,
        connect, connectSize,
        &pinHandle, sizeof(pinHandle),
        &bytesReturned, nullptr);

    DWORD err = GetLastError();
    free(connect);

    if (result && pinHandle != INVALID_HANDLE_VALUE) {
        printf("  -> PIN CREATED SUCCESSFULLY! handle=%p\n", pinHandle);
        CloseHandle(pinHandle);
        return true;
    }
    
    printf("  -> Failed: error %d (0x%08X)\n", err, err);
    
    // Also try plain WAVEFORMATEX
    printf("  Trying plain WAVEFORMATEX...\n");
    
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = (WORD)channels;
    wfx.nSamplesPerSec = sampleRate;
    wfx.wBitsPerSample = (WORD)bitsPerSample;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    DWORD connectSize2 = sizeof(KSPIN_CONNECT) + sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEX);
    auto* connect2 = (KSPIN_CONNECT*)calloc(1, connectSize2);
    *connect2 = *(KSPIN_CONNECT*)connect; // reuse same settings
    connect2->PinId = pinId;
    connect2->Interface.Set = KSINTERFACESETID_Standard;
    connect2->Interface.Id = KSINTERFACE_STANDARD_STREAMING;
    connect2->Medium.Set = KSMEDIUMSETID_Standard;
    connect2->Medium.Id = KSMEDIUM_TYPE_ANYINSTANCE;
    connect2->Priority.PriorityClass = KSPRIORITY_NORMAL;
    connect2->Priority.PrioritySubClass = 1;

    dataFormat = (KSDATAFORMAT*)(connect2 + 1);
    dataFormat->MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
    dataFormat->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    dataFormat->Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
    dataFormat->FormatSize = sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEX);
    dataFormat->SampleSize = wfx.nBlockAlign;

    memcpy(dataFormat + 1, &wfx, sizeof(WAVEFORMATEX));

    pinHandle = INVALID_HANDLE_VALUE;
    result = DeviceIoControl(hFilter,
        IOCTL_KS_CREATE_PIN,
        connect2, connectSize2,
        &pinHandle, sizeof(pinHandle),
        &bytesReturned, nullptr);

    err = GetLastError();
    free(connect2);

    if (result && pinHandle != INVALID_HANDLE_VALUE) {
        printf("  -> PIN CREATED with WAVEFORMATEX! handle=%p\n", pinHandle);
        CloseHandle(pinHandle);
        return true;
    }
    printf("  -> WAVEFORMATEX also failed: error %d\n", err);
    return false;
}

int main() {
    printf("=== KS Pin Creation Test ===\n");

    // Test capture device
    auto capPath = findDevice(KSCATEGORY_CAPTURE, L"pcm_in_");
    if (capPath.empty()) {
        printf("No capture device found!\n");
        return 1;
    }
    printf("\nCapture: %ls\n", capPath.c_str());

    HANDLE hCap = CreateFileW(capPath.c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (hCap == INVALID_HANDLE_VALUE) {
        printf("Failed to open capture filter: %d\n", GetLastError());
        return 1;
    }

    // Query pin data ranges to see what formats are supported
    printf("\n  Querying capture pin data ranges:\n");
    queryPinDataRanges(hCap, 0);

    // Try creating pin with various formats
    tryCreatePin(hCap, 0, 8, 24, 48000);
    tryCreatePin(hCap, 0, 8, 24, 44100);
    tryCreatePin(hCap, 0, 2, 24, 48000);
    tryCreatePin(hCap, 0, 2, 16, 48000);

    CloseHandle(hCap);

    // Also test render
    auto renPath = findDevice(KSCATEGORY_RENDER, L"pcm_out_");
    if (!renPath.empty()) {
        printf("\n\nRender: %ls\n", renPath.c_str());
        HANDLE hRen = CreateFileW(renPath.c_str(),
            GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
        if (hRen != INVALID_HANDLE_VALUE) {
            printf("\n  Querying render pin data ranges:\n");
            queryPinDataRanges(hRen, 0);
            tryCreatePin(hRen, 0, 10, 24, 48000);
            tryCreatePin(hRen, 0, 2, 24, 48000);
            tryCreatePin(hRen, 0, 2, 16, 48000);
            CloseHandle(hRen);
        }
    }

    printf("\n=== Done ===\n");
    return 0;
}
