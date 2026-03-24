#include <windows.h>
#include <setupapi.h>
#include <ks.h>
#include <ksmedia.h>
#include <stdio.h>
#include <mmreg.h>
#include <devioctl.h>

#pragma comment(lib, "setupapi.lib")

#ifndef IOCTL_KS_CREATE_PIN
#define IOCTL_KS_CREATE_PIN CTL_CODE(FILE_DEVICE_KS, 0x003, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif

int main() {
    const wchar_t* paths[] = {
        L"\\\\?\\tusbaudio_enum#vid_1397&pid_0503&ks#f2e70f04#{65e8773d-8f56-11d0-a3b9-00a0c9223196}\\pcm_in_01_c_08_sd5_48000",
        L"\\\\?\\tusbaudio_enum#vid_1397&pid_0503&ks#f2e70f04#{65e8773e-8f56-11d0-a3b9-00a0c9223196}\\pcm_out_01_c_10_sd6_48000"
    };
    const char* names[] = {"Capture (8ch)", "Render (10ch)"};
    int chCounts[] = {8, 10};
    
    for (int p = 0; p < 2; p++) {
        printf("\n=== %s ===\n", names[p]);
        
        HANDLE hFilter = CreateFileW(paths[p],
            GENERIC_READ | GENERIC_WRITE, 0, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
        if (hFilter == INVALID_HANDLE_VALUE) { printf("  Open fail: %lu\n", GetLastError()); continue; }
        
        KSP_PIN ksPinProp = {};
        ksPinProp.Property.Set = KSPROPSETID_Pin;
        ksPinProp.Property.Id = KSPROPERTY_PIN_DATARANGES;
        ksPinProp.Property.Flags = KSPROPERTY_TYPE_GET;
        ksPinProp.PinId = 0;
        
        BYTE rangeBuf[4096];
        DWORD br = 0;
        if (DeviceIoControl(hFilter, IOCTL_KS_PROPERTY,
            &ksPinProp, sizeof(ksPinProp), rangeBuf, sizeof(rangeBuf), &br, NULL)) {
            KSMULTIPLE_ITEM* multi = (KSMULTIPLE_ITEM*)rangeBuf;
            BYTE* ptr = rangeBuf + sizeof(KSMULTIPLE_ITEM);
            for (ULONG r = 0; r < multi->Count; r++) {
                KSDATARANGE* range = (KSDATARANGE*)ptr;
                if (range->FormatSize == sizeof(KSDATARANGE_AUDIO)) {
                    KSDATARANGE_AUDIO* a = (KSDATARANGE_AUDIO*)ptr;
                    printf("  Range[%u]: MaxCh=%u, Bits=%u-%u, Rate=%u-%u\n",
                           r, a->MaximumChannels,
                           a->MinimumBitsPerSample, a->MaximumBitsPerSample,
                           a->MinimumSampleFrequency, a->MaximumSampleFrequency);
                }
                ptr += range->FormatSize;
                ptr = (BYTE*)(((ULONG_PTR)ptr + 7) & ~7);
            }
        }
        
        // 尝试多种格式创建 Pin
        struct { int ch; int bits; int rate; int container; bool extensible; } fmts[] = {
            {chCounts[p], 24, 48000, 32, true},
            {chCounts[p], 24, 48000, 24, true},
            {chCounts[p], 24, 48000, 32, false},
            {chCounts[p], 24, 48000, 24, false},
            {chCounts[p], 16, 48000, 16, true},
            {chCounts[p], 16, 48000, 16, false},
            {2, 24, 48000, 32, true},
            {2, 16, 48000, 16, false},
        };
        
        for (int f = 0; f < 8; f++) {
            auto& fmt = fmts[f];
            
            BYTE buf[512] = {};
            KSPIN_CONNECT* conn = (KSPIN_CONNECT*)buf;
            conn->PinId = 0;
            conn->Interface.Set = KSINTERFACESETID_Standard;
            conn->Interface.Id = KSINTERFACE_STANDARD_STREAMING;
            conn->Medium.Set = KSMEDIUMSETID_Standard;
            conn->Medium.Id = KSMEDIUM_TYPE_ANYINSTANCE;
            conn->Priority.PriorityClass = KSPRIORITY_NORMAL;
            conn->Priority.PrioritySubClass = 1;
            
            DWORD totalSize;
            KSDATAFORMAT* df = (KSDATAFORMAT*)(buf + sizeof(KSPIN_CONNECT));
            df->MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
            df->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            df->Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
            
            if (fmt.extensible) {
                WAVEFORMATEXTENSIBLE wfext = {};
                wfext.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
                wfext.Format.nChannels = (WORD)fmt.ch;
                wfext.Format.nSamplesPerSec = fmt.rate;
                wfext.Format.wBitsPerSample = (WORD)fmt.container;
                wfext.Format.nBlockAlign = wfext.Format.nChannels * wfext.Format.wBitsPerSample / 8;
                wfext.Format.nAvgBytesPerSec = wfext.Format.nSamplesPerSec * wfext.Format.nBlockAlign;
                wfext.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
                wfext.Samples.wValidBitsPerSample = (WORD)fmt.bits;
                wfext.dwChannelMask = (1 << fmt.ch) - 1;
                wfext.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
                
                df->FormatSize = sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEXTENSIBLE);
                df->SampleSize = wfext.Format.nBlockAlign;
                memcpy(buf + sizeof(KSPIN_CONNECT) + sizeof(KSDATAFORMAT), &wfext, sizeof(wfext));
                totalSize = sizeof(KSPIN_CONNECT) + sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEXTENSIBLE);
            } else {
                WAVEFORMATEX wfx = {};
                wfx.wFormatTag = WAVE_FORMAT_PCM;
                wfx.nChannels = (WORD)fmt.ch;
                wfx.nSamplesPerSec = fmt.rate;
                wfx.wBitsPerSample = (WORD)fmt.bits;
                wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
                wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
                wfx.cbSize = 0;
                
                df->FormatSize = sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEX);
                df->SampleSize = wfx.nBlockAlign;
                memcpy(buf + sizeof(KSPIN_CONNECT) + sizeof(KSDATAFORMAT), &wfx, sizeof(wfx));
                totalSize = sizeof(KSPIN_CONNECT) + sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEX);
            }
            
            HANDLE hPin = INVALID_HANDLE_VALUE;
            BOOL ok = DeviceIoControl(hFilter, IOCTL_KS_CREATE_PIN,
                buf, totalSize, &hPin, sizeof(hPin), &br, NULL);
            
            printf("  %dch/%dbit(c%d)/%dHz %s: %s (err=%lu)\n",
                   fmt.ch, fmt.bits, fmt.container, fmt.rate,
                   fmt.extensible ? "EXT" : "PCM",
                   ok ? "OK!" : "FAIL", ok ? 0 : GetLastError());
            
            if (ok && hPin != INVALID_HANDLE_VALUE) {
                printf("    *** SUCCESS! ***\n");
                CloseHandle(hPin);
            }
        }
        
        CloseHandle(hFilter);
    }
    return 0;
}
