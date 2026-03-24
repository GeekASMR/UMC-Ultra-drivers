/*
 * Probe the actual TUsbAudioChannelProperty struct size
 * by dumping raw bytes and finding channel name patterns
 */
#include <windows.h>
#include <stdio.h>
#include "src/driver/TUsbAudioApi.h"

int main() {
    TUsbAudioApi api;
    if (!api.load(L"d:\\UMCasio\\official_driver\\named_files\\umc_audioapi_x64.dll")) {
        printf("Failed to load API DLL\n");
        return 1;
    }

    api.fn.TUSBAUDIO_EnumerateDevices();
    TUsbAudioHandle handle = nullptr;
    api.fn.TUSBAUDIO_OpenDeviceByIndex(0, &handle);
    if (!handle) { printf("No device\n"); return 1; }

    // Allocate a big raw buffer for channel properties
    unsigned char rawBuf[32768];
    memset(rawBuf, 0xCC, sizeof(rawBuf)); // Fill with marker byte

    unsigned int chCount = 0;
    TUsbAudioStatus st = api.fn.TUSBAUDIO_GetChannelProperties(
        handle, 0, // output channels
        32,
        (TUsbAudioChannelProperty*)rawBuf,
        &chCount);

    printf("GetChannelProperties returned: 0x%08X, count=%d\n\n", st, chCount);

    if (st != TSTATUS_SUCCESS || chCount == 0) {
        api.fn.TUSBAUDIO_CloseDevice(handle);
        return 1;
    }

    // Our struct size
    printf("Our TUsbAudioChannelProperty size: %llu bytes\n\n", sizeof(TUsbAudioChannelProperty));

    // Search for "Out" or "In" pattern (UTF-16 LE) in the raw buffer to find struct boundaries
    // "Out 1" in UTF-16LE: 'O'=4F00, 'u'=7500, 't'=7400, ' '=2000, '1'=3100
    // "In 1" in UTF-16LE: 'I'=4900, 'n'=6E00, ' '=2000, '1'=3100

    printf("Searching for 'Out' and 'In' patterns in raw buffer:\n");
    for (int offset = 0; offset < 8192; offset++) {
        // Search for "Out" (UTF-16LE)
        if (rawBuf[offset] == 'O' && rawBuf[offset+1] == 0 &&
            rawBuf[offset+2] == 'u' && rawBuf[offset+3] == 0 &&
            rawBuf[offset+4] == 't' && rawBuf[offset+5] == 0) {
            printf("  Found 'Out' at offset %d (0x%X)\n", offset, offset);
        }
        // Search for "In" (UTF-16LE) 
        if (rawBuf[offset] == 'I' && rawBuf[offset+1] == 0 &&
            rawBuf[offset+2] == 'n' && rawBuf[offset+3] == 0 &&
            rawBuf[offset+4] == ' ' && rawBuf[offset+5] == 0) {
            printf("  Found 'In ' at offset %d (0x%X)\n", offset, offset);
        }
    }

    // Also dump the first 2048 bytes as hex to visually inspect struct layout
    printf("\n--- Raw buffer hex dump (first 2048 bytes) ---\n");
    for (int row = 0; row < 128; row++) {
        int off = row * 16;
        printf("%04X: ", off);
        for (int col = 0; col < 16; col++) {
            printf("%02X ", rawBuf[off + col]);
        }
        printf(" | ");
        for (int col = 0; col < 16; col++) {
            unsigned char c = rawBuf[off + col];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("\n");
    }

    api.fn.TUSBAUDIO_CloseDevice(handle);
    return 0;
}
