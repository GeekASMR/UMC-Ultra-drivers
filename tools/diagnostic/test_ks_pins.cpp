#include <windows.h>
#include <setupapi.h>
#include <ks.h>
#include <ksmedia.h>
#include <stdio.h>

#pragma comment(lib, "setupapi.lib")

int main() {
    // 直接打开已知的 KS 设备路径
    const wchar_t* capturePath = L"\\\\?\\tusbaudio_enum#vid_1397&pid_0503&ks#f2e70f04#{65e8773d-8f56-11d0-a3b9-00a0c9223196}\\pcm_in_01_c_08_sd5_48000";
    const wchar_t* renderPath = L"\\\\?\\tusbaudio_enum#vid_1397&pid_0503&ks#f2e70f04#{65e8773e-8f56-11d0-a3b9-00a0c9223196}\\pcm_out_01_c_10_sd6_48000";
    
    const wchar_t* paths[] = {capturePath, renderPath};
    const char* names[] = {"Capture (8ch)", "Render (10ch)"};
    
    for (int p = 0; p < 2; p++) {
        printf("\n=== %s ===\n", names[p]);
        
        HANDLE hFilter = CreateFileW(paths[p],
            GENERIC_READ | GENERIC_WRITE, 0, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
        
        if (hFilter == INVALID_HANDLE_VALUE) {
            printf("  Open failed: %lu\n", GetLastError());
            continue;
        }
        printf("  Opened OK\n");
        
        // Query pin count
        KSP_PIN ksPinProp;
        memset(&ksPinProp, 0, sizeof(ksPinProp));
        ksPinProp.Property.Set = KSPROPSETID_Pin;
        ksPinProp.Property.Id = KSPROPERTY_PIN_CTYPES;
        ksPinProp.Property.Flags = KSPROPERTY_TYPE_GET;
        
        ULONG pinCount = 0;
        DWORD br = 0;
        BOOL ok = DeviceIoControl(hFilter, IOCTL_KS_PROPERTY,
            &ksPinProp, sizeof(KSPROPERTY), &pinCount, sizeof(pinCount), &br, NULL);
        printf("  Pin count: %lu (ok=%d, err=%lu)\n", pinCount, ok, ok ? 0 : GetLastError());
        
        // Query each pin's dataflow and communication
        for (ULONG pin = 0; pin < pinCount && pin < 8; pin++) {
            // DataFlow
            memset(&ksPinProp, 0, sizeof(ksPinProp));
            ksPinProp.Property.Set = KSPROPSETID_Pin;
            ksPinProp.Property.Id = KSPROPERTY_PIN_DATAFLOW;
            ksPinProp.Property.Flags = KSPROPERTY_TYPE_GET;
            ksPinProp.PinId = pin;
            
            KSPIN_DATAFLOW flow = (KSPIN_DATAFLOW)0;
            ok = DeviceIoControl(hFilter, IOCTL_KS_PROPERTY,
                &ksPinProp, sizeof(ksPinProp), &flow, sizeof(flow), &br, NULL);
            
            // Communication
            ksPinProp.Property.Id = KSPROPERTY_PIN_COMMUNICATION;
            KSPIN_COMMUNICATION comm = (KSPIN_COMMUNICATION)0;
            DeviceIoControl(hFilter, IOCTL_KS_PROPERTY,
                &ksPinProp, sizeof(ksPinProp), &comm, sizeof(comm), &br, NULL);
            
            const char* flowStr = flow == KSPIN_DATAFLOW_IN ? "IN(Sink)" : 
                                  flow == KSPIN_DATAFLOW_OUT ? "OUT(Source)" : "?";
            const char* commStr = comm == KSPIN_COMMUNICATION_SINK ? "SINK" :
                                  comm == KSPIN_COMMUNICATION_SOURCE ? "SOURCE" :
                                  comm == KSPIN_COMMUNICATION_BOTH ? "BOTH" :
                                  comm == KSPIN_COMMUNICATION_NONE ? "NONE" :
                                  comm == KSPIN_COMMUNICATION_BRIDGE ? "BRIDGE" : "?";
            
            printf("  Pin[%lu]: Flow=%s  Comm=%s\n", pin, flowStr, commStr);
            
            // Try to get data ranges (format support)
            ksPinProp.Property.Id = KSPROPERTY_PIN_DATARANGES;
            BYTE rangeBuf[4096];
            DWORD rangeSize = 0;
            ok = DeviceIoControl(hFilter, IOCTL_KS_PROPERTY,
                &ksPinProp, sizeof(ksPinProp), rangeBuf, sizeof(rangeBuf), &rangeSize, NULL);
            if (ok && rangeSize >= sizeof(KSMULTIPLE_ITEM)) {
                KSMULTIPLE_ITEM* multi = (KSMULTIPLE_ITEM*)rangeBuf;
                printf("    DataRanges: %lu items, %lu bytes\n", multi->Count, multi->Size);
                
                BYTE* ptr = rangeBuf + sizeof(KSMULTIPLE_ITEM);
                for (ULONG r = 0; r < multi->Count && r < 4; r++) {
                    KSDATARANGE* range = (KSDATARANGE*)ptr;
                    printf("    Range[%lu]: FormatSize=%lu SampleSize=%lu\n", 
                           r, range->FormatSize, range->SampleSize);
                    
                    if (range->FormatSize >= sizeof(KSDATAFORMAT_WAVEFORMATEX)) {
                        KSDATAFORMAT_WAVEFORMATEX* wfmt = (KSDATAFORMAT_WAVEFORMATEX*)ptr;
                        printf("      WFX: %dch %dHz %dbit\n",
                               wfmt->WaveFormatEx.nChannels,
                               wfmt->WaveFormatEx.nSamplesPerSec,
                               wfmt->WaveFormatEx.wBitsPerSample);
                    }
                    ptr += range->FormatSize;
                    // Align to 8 bytes
                    ptr = (BYTE*)(((ULONG_PTR)ptr + 7) & ~7);
                }
            }
        }
        
        CloseHandle(hFilter);
    }
    
    printf("\nDone.\n");
    return 0;
}
