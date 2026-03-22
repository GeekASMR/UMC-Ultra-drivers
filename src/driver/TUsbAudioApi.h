/*
 * TUsbAudioApi.h - Thesycon TUSBAudio API declarations
 * 
 * Dynamic loader for umc_audioapi_x64.dll (tusbaudioapi.dll)
 * This is the official API layer between user-mode and the UMC kernel driver.
 * 
 * Based on Thesycon TUSBAudio Reference Manual v1.22.0
 */

#pragma once

#include <windows.h>

// ============================================================================
// Constants
// ============================================================================

#define TUSBAUDIO_API_VERSION_MJ    3
#define TUSBAUDIO_API_VERSION_MN    0

#define TUSBAUDIO_MAX_STRDESC_STRLEN  160  // Verified by binary probe: struct stride=340, header=20, name=320/2=160

// Status codes
typedef unsigned int TUsbAudioStatus;
#define TSTATUS_SUCCESS             0x00000000
#define TSTATUS_INVALID_PARAMETER   0x00000001
#define TSTATUS_INVALID_HANDLE      0x00000002
#define TSTATUS_NOT_AVAILABLE       0x00000003
#define TSTATUS_ENUM_REQUIRED       0x00000004
#define TSTATUS_NO_MORE_ITEMS       0x00000005
#define TSTATUS_BUFFER_TOO_SMALL    0x00000006
#define TSTATUS_GENERAL_ERROR       0x0000FFFF

// Device property flags
#define TUSBAUDIO_DEVPROP_FLAG_HIGH_SPEED_SUPPORTED     0x0001
#define TUSBAUDIO_DEVPROP_FLAG_HIGH_SPEED               0x0002
#define TUSBAUDIO_DEVPROP_FLAG_DFU_SUPPORTED            0x0004
#define TUSBAUDIO_DEVPROP_FLAG_DSP_PLUGIN_PRESENT       0x0008
#define TUSBAUDIO_DEVPROP_FLAG_AUDIOCLASS10_SUPPORTED    0x0010
#define TUSBAUDIO_DEVPROP_FLAG_AUDIOCLASS20_SUPPORTED    0x0020

// Channel property flags
#define TUSBAUDIO_CHANPROP_FLAG_VOLUME_MAPPED  0x0001
#define TUSBAUDIO_CHANPROP_FLAG_MUTE_MAPPED    0x0002

// PnP notification messages
#define TUSBAUDIO_DEVICE_ARRIVAL_MSG  1
#define TUSBAUDIO_DEVICE_REMOVED_MSG  2

// Notification categories
#define TUSBAUDIO_NOTIFY_CATEGORY_NONE                  0x0000
#define TUSBAUDIO_NOTIFY_CATEGORY_ALL                   0xFFFF
#define TUSBAUDIO_NOTIFY_CATEGORY_SAMPLE_RATE_CHANGE    0x0001
#define TUSBAUDIO_NOTIFY_CATEGORY_STREAM_CHANGE         0x0002
#define TUSBAUDIO_NOTIFY_CATEGORY_VOLUME_CHANGE         0x0004
#define TUSBAUDIO_NOTIFY_CATEGORY_AC_NODE_INTERRUPT     0x0008

#define TUSBAUDIO_NOTIFY_EVENT_MAX_DATA_BYTES  64

// Device handle type
typedef void* TUsbAudioHandle;

// ============================================================================
// Structures
// ============================================================================

typedef struct {
    unsigned int usbVendorId;
    unsigned int usbProductId;
    unsigned int usbRevisionId;
    wchar_t serialNumberString[TUSBAUDIO_MAX_STRDESC_STRLEN];
    wchar_t manufacturerString[TUSBAUDIO_MAX_STRDESC_STRLEN];
    wchar_t productString[TUSBAUDIO_MAX_STRDESC_STRLEN];
    unsigned int flags;
} TUsbAudioDeviceProperties;

typedef struct {
    unsigned int clockSourceId;
    unsigned int clockSourceUnitId;
    unsigned int clockSelectorPinNumber;
    unsigned int clockIsValid;
    unsigned int sampleRate;
    wchar_t clockNameString[TUSBAUDIO_MAX_STRDESC_STRLEN];
} TUsbAudioClockSource;

typedef struct {
    unsigned int formatId;
    unsigned int bitsPerSample;
    unsigned int numberOfChannels;
    wchar_t formatNameString[TUSBAUDIO_MAX_STRDESC_STRLEN];
} TUsbAudioStreamFormat;

typedef struct {
    unsigned int channelIndex;
    unsigned int isInput;
    unsigned int flags;
    unsigned char featureUnitId;
    unsigned char featureUnitLogicalChannel;
    short volumeRangeMin;
    short volumeRangeMax;
    unsigned short volumeRangeStep;
    wchar_t channelNameString[TUSBAUDIO_MAX_STRDESC_STRLEN];
} TUsbAudioChannelProperty;

typedef struct {
    unsigned int apiVersionMajor;
    unsigned int apiVersionMinor;
    unsigned int driverVersionMajor;
    unsigned int driverVersionMinor;
    unsigned int driverVersionSub;
    unsigned int flags;
} TUsbAudioDriverInfo;

typedef enum {
    NotifyEvent_SampleRateChanged,
    NotifyEvent_StreamFormatChanged,
    NotifyEvent_AcNodeInterrupt,
    NotifyEvent_VolumeChanged,
    NotifyEvent_MuteChanged
} TUsbAudioNotifyEvent;

// ============================================================================
// Function pointer types (for dynamic loading)
// ============================================================================

// Core API
typedef unsigned int   (__cdecl *PFN_TUSBAUDIO_GetApiVersion)();
typedef int            (__cdecl *PFN_TUSBAUDIO_CheckApiVersion)(unsigned long majorVersion, unsigned long minorVersion);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_EnumerateDevices)();
typedef unsigned int   (__cdecl *PFN_TUSBAUDIO_GetDeviceCount)();
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_OpenDeviceByIndex)(unsigned int deviceIndex, TUsbAudioHandle* deviceHandle);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_CloseDevice)(TUsbAudioHandle deviceHandle);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetDeviceProperties)(TUsbAudioHandle deviceHandle, TUsbAudioDeviceProperties* properties);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetDriverInfo)(TUsbAudioDriverInfo* driverInfo);

// Sample rate
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetSupportedSampleRates)(TUsbAudioHandle deviceHandle, unsigned int sampleRateMaxCount, unsigned int sampleRateArray[], unsigned int* sampleRateCount);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetCurrentSampleRate)(TUsbAudioHandle deviceHandle, unsigned int* sampleRate);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_SetSampleRate)(TUsbAudioHandle deviceHandle, unsigned int sampleRate);

// Stream formats
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetSupportedStreamFormats)(TUsbAudioHandle deviceHandle, unsigned int inputStream, unsigned int formatMaxCount, TUsbAudioStreamFormat formatArray[], unsigned int* formatCount);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetCurrentStreamFormat)(TUsbAudioHandle deviceHandle, unsigned int inputStream, unsigned int* format);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_SetCurrentStreamFormat)(TUsbAudioHandle deviceHandle, unsigned int inputStream, unsigned int formatId);

// Channel info
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetChannelProperties)(TUsbAudioHandle deviceHandle, unsigned int inputChannels, unsigned int channelMaxCount, TUsbAudioChannelProperty channelArray[], unsigned int* channelCount);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetStreamChannelCount)(TUsbAudioHandle deviceHandle, unsigned int inputStream, unsigned int* channelCount);

// Buffer sizes
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetAsioBufferSizeSet)(unsigned int bufferSizeMaxCount, unsigned int bufferSizeArray_us[], unsigned int* bufferSizeCount, unsigned int* bufferSizeCurrentIndex);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetAsioBufferSize)(unsigned int* sizeMicroseconds);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_SetAsioBufferSize)(unsigned int sizeMicroseconds);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_SetASIOBufferPreferredSize)(unsigned int sizeMicroseconds);

// Clock sources
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetSupportedClockSources)(TUsbAudioHandle deviceHandle, unsigned int clockSourceMaxCount, TUsbAudioClockSource clockSourceArray[], unsigned int* clockSourceCount);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetCurrentClockSource)(TUsbAudioHandle deviceHandle, TUsbAudioClockSource* clockSource);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_SetCurrentClockSource)(TUsbAudioHandle deviceHandle, unsigned int clockSourceId);

// Notifications
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_RegisterPnpNotification)(HANDLE deviceArrivalEvent, HANDLE deviceRemovedEvent, void* windowHandle, unsigned int windowMsgCode, unsigned int flags);
typedef void            (__cdecl *PFN_TUSBAUDIO_UnregisterPnpNotification)();
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_RegisterDeviceNotification)(TUsbAudioHandle deviceHandle, unsigned int categoryFilter, HANDLE sharedEvent, unsigned int flags);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_ReadDeviceNotification)(TUsbAudioHandle deviceHandle, TUsbAudioNotifyEvent* eventType, unsigned char* dataBuffer, unsigned int dataBufferSize, unsigned int* dataBytesReturned);

// Volume / Mute
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetVolume)(TUsbAudioHandle deviceHandle, unsigned char entityID, unsigned char channel, short* volume);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_SetVolume)(TUsbAudioHandle deviceHandle, unsigned char entityID, unsigned char channel, short volume);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetMute)(TUsbAudioHandle deviceHandle, unsigned char entityID, unsigned char channel, unsigned int* muted);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_SetMute)(TUsbAudioHandle deviceHandle, unsigned char entityID, unsigned char channel, unsigned int muted);

// Status code to string
typedef const char*    (__cdecl *PFN_TUSBAUDIO_StatusCodeStringA)(TUsbAudioStatus statusCode);
typedef const wchar_t* (__cdecl *PFN_TUSBAUDIO_StatusCodeStringW)(TUsbAudioStatus statusCode);

// ASIO related  
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetASIOInstanceCount)(unsigned int* count);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetASIORelation)(TUsbAudioHandle deviceHandle, unsigned int* asioInstanceIndex);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_GetDeviceStreamingMode)(TUsbAudioHandle deviceHandle, unsigned int* mode);
typedef TUsbAudioStatus (__cdecl *PFN_TUSBAUDIO_SetDeviceStreamingMode)(TUsbAudioHandle deviceHandle, unsigned int mode);

// ============================================================================
// Dynamic loader class
// ============================================================================

class TUsbAudioApi {
public:
    TUsbAudioApi() : m_hModule(nullptr) { memset(&fn, 0, sizeof(fn)); }
    ~TUsbAudioApi() { unload(); }

    bool load(const wchar_t* dllPath = nullptr) {
        if (m_hModule) return true;

        // Try paths in order
        const wchar_t* paths[] = {
            dllPath,
            L"umc_audioapi_x64.dll",           // Same dir as our DLL
            L"C:\\Program Files\\BEHRINGER\\UMC\\umc_audioapi_x64.dll",
            nullptr
        };

        for (int i = 0; paths[i]; i++) {
            m_hModule = LoadLibraryW(paths[i]);
            if (m_hModule) break;
        }

        if (!m_hModule) return false;

        // Load all function pointers
        #define LOAD_FN(name) fn.name = (PFN_##name)GetProcAddress(m_hModule, #name)

        LOAD_FN(TUSBAUDIO_GetApiVersion);
        LOAD_FN(TUSBAUDIO_CheckApiVersion);
        LOAD_FN(TUSBAUDIO_EnumerateDevices);
        LOAD_FN(TUSBAUDIO_GetDeviceCount);
        LOAD_FN(TUSBAUDIO_OpenDeviceByIndex);
        LOAD_FN(TUSBAUDIO_CloseDevice);
        LOAD_FN(TUSBAUDIO_GetDeviceProperties);
        LOAD_FN(TUSBAUDIO_GetDriverInfo);
        LOAD_FN(TUSBAUDIO_GetSupportedSampleRates);
        LOAD_FN(TUSBAUDIO_GetCurrentSampleRate);
        LOAD_FN(TUSBAUDIO_SetSampleRate);
        LOAD_FN(TUSBAUDIO_GetSupportedStreamFormats);
        LOAD_FN(TUSBAUDIO_GetCurrentStreamFormat);
        LOAD_FN(TUSBAUDIO_SetCurrentStreamFormat);
        LOAD_FN(TUSBAUDIO_GetChannelProperties);
        LOAD_FN(TUSBAUDIO_GetStreamChannelCount);
        LOAD_FN(TUSBAUDIO_GetAsioBufferSizeSet);
        LOAD_FN(TUSBAUDIO_GetAsioBufferSize);
        LOAD_FN(TUSBAUDIO_SetAsioBufferSize);
        LOAD_FN(TUSBAUDIO_SetASIOBufferPreferredSize);
        LOAD_FN(TUSBAUDIO_GetSupportedClockSources);
        LOAD_FN(TUSBAUDIO_GetCurrentClockSource);
        LOAD_FN(TUSBAUDIO_SetCurrentClockSource);
        LOAD_FN(TUSBAUDIO_RegisterPnpNotification);
        LOAD_FN(TUSBAUDIO_UnregisterPnpNotification);
        LOAD_FN(TUSBAUDIO_RegisterDeviceNotification);
        LOAD_FN(TUSBAUDIO_ReadDeviceNotification);
        LOAD_FN(TUSBAUDIO_GetVolume);
        LOAD_FN(TUSBAUDIO_SetVolume);
        LOAD_FN(TUSBAUDIO_GetMute);
        LOAD_FN(TUSBAUDIO_SetMute);
        LOAD_FN(TUSBAUDIO_StatusCodeStringA);
        LOAD_FN(TUSBAUDIO_StatusCodeStringW);
        LOAD_FN(TUSBAUDIO_GetASIOInstanceCount);
        LOAD_FN(TUSBAUDIO_GetASIORelation);
        LOAD_FN(TUSBAUDIO_GetDeviceStreamingMode);
        LOAD_FN(TUSBAUDIO_SetDeviceStreamingMode);

        #undef LOAD_FN

        // Verify critical functions
        return (fn.TUSBAUDIO_EnumerateDevices &&
                fn.TUSBAUDIO_GetDeviceCount &&
                fn.TUSBAUDIO_OpenDeviceByIndex &&
                fn.TUSBAUDIO_CloseDevice);
    }

    void unload() {
        if (m_hModule) {
            FreeLibrary(m_hModule);
            m_hModule = nullptr;
            memset(&fn, 0, sizeof(fn));
        }
    }

    bool isLoaded() const { return m_hModule != nullptr; }

    // Function pointers struct - direct access for callers
    struct {
        PFN_TUSBAUDIO_GetApiVersion            TUSBAUDIO_GetApiVersion;
        PFN_TUSBAUDIO_CheckApiVersion          TUSBAUDIO_CheckApiVersion;
        PFN_TUSBAUDIO_EnumerateDevices         TUSBAUDIO_EnumerateDevices;
        PFN_TUSBAUDIO_GetDeviceCount           TUSBAUDIO_GetDeviceCount;
        PFN_TUSBAUDIO_OpenDeviceByIndex        TUSBAUDIO_OpenDeviceByIndex;
        PFN_TUSBAUDIO_CloseDevice              TUSBAUDIO_CloseDevice;
        PFN_TUSBAUDIO_GetDeviceProperties      TUSBAUDIO_GetDeviceProperties;
        PFN_TUSBAUDIO_GetDriverInfo            TUSBAUDIO_GetDriverInfo;
        PFN_TUSBAUDIO_GetSupportedSampleRates  TUSBAUDIO_GetSupportedSampleRates;
        PFN_TUSBAUDIO_GetCurrentSampleRate     TUSBAUDIO_GetCurrentSampleRate;
        PFN_TUSBAUDIO_SetSampleRate            TUSBAUDIO_SetSampleRate;
        PFN_TUSBAUDIO_GetSupportedStreamFormats TUSBAUDIO_GetSupportedStreamFormats;
        PFN_TUSBAUDIO_GetCurrentStreamFormat   TUSBAUDIO_GetCurrentStreamFormat;
        PFN_TUSBAUDIO_SetCurrentStreamFormat   TUSBAUDIO_SetCurrentStreamFormat;
        PFN_TUSBAUDIO_GetChannelProperties     TUSBAUDIO_GetChannelProperties;
        PFN_TUSBAUDIO_GetStreamChannelCount    TUSBAUDIO_GetStreamChannelCount;
        PFN_TUSBAUDIO_GetAsioBufferSizeSet     TUSBAUDIO_GetAsioBufferSizeSet;
        PFN_TUSBAUDIO_GetAsioBufferSize        TUSBAUDIO_GetAsioBufferSize;
        PFN_TUSBAUDIO_SetAsioBufferSize        TUSBAUDIO_SetAsioBufferSize;
        PFN_TUSBAUDIO_SetASIOBufferPreferredSize TUSBAUDIO_SetASIOBufferPreferredSize;
        PFN_TUSBAUDIO_GetSupportedClockSources TUSBAUDIO_GetSupportedClockSources;
        PFN_TUSBAUDIO_GetCurrentClockSource    TUSBAUDIO_GetCurrentClockSource;
        PFN_TUSBAUDIO_SetCurrentClockSource    TUSBAUDIO_SetCurrentClockSource;
        PFN_TUSBAUDIO_RegisterPnpNotification  TUSBAUDIO_RegisterPnpNotification;
        PFN_TUSBAUDIO_UnregisterPnpNotification TUSBAUDIO_UnregisterPnpNotification;
        PFN_TUSBAUDIO_RegisterDeviceNotification TUSBAUDIO_RegisterDeviceNotification;
        PFN_TUSBAUDIO_ReadDeviceNotification   TUSBAUDIO_ReadDeviceNotification;
        PFN_TUSBAUDIO_GetVolume                TUSBAUDIO_GetVolume;
        PFN_TUSBAUDIO_SetVolume                TUSBAUDIO_SetVolume;
        PFN_TUSBAUDIO_GetMute                  TUSBAUDIO_GetMute;
        PFN_TUSBAUDIO_SetMute                  TUSBAUDIO_SetMute;
        PFN_TUSBAUDIO_StatusCodeStringA        TUSBAUDIO_StatusCodeStringA;
        PFN_TUSBAUDIO_StatusCodeStringW        TUSBAUDIO_StatusCodeStringW;
        PFN_TUSBAUDIO_GetASIOInstanceCount     TUSBAUDIO_GetASIOInstanceCount;
        PFN_TUSBAUDIO_GetASIORelation          TUSBAUDIO_GetASIORelation;
        PFN_TUSBAUDIO_GetDeviceStreamingMode   TUSBAUDIO_GetDeviceStreamingMode;
        PFN_TUSBAUDIO_SetDeviceStreamingMode   TUSBAUDIO_SetDeviceStreamingMode;
    } fn;

private:
    HMODULE m_hModule;
};
