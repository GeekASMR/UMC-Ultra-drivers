/*
 * test_tusbaudio.cpp - Quick test for TUSBAUDIO API integration
 * Verifies we can load the official API DLL and communicate with the UMC driver
 */

#include <windows.h>
#include <stdio.h>
#include "src/driver/TUsbAudioApi.h"

int main() {
    printf("=== TUSBAUDIO API Test ===\n\n");

    TUsbAudioApi api;

    // Try to find and load the DLL
    // Check if it's in System32 first
    wchar_t sysDir[MAX_PATH];
    GetSystemDirectoryW(sysDir, MAX_PATH);

    const wchar_t* searchPaths[] = {
        L"umc_audioapi_x64.dll",
        L"d:\\UMCasio\\official_driver\\named_files\\umc_audioapi_x64.dll",
        nullptr
    };

    bool loaded = false;
    for (int i = 0; searchPaths[i]; i++) {
        printf("Trying: %ls ... ", searchPaths[i]);
        if (api.load(searchPaths[i])) {
            printf("OK!\n");
            loaded = true;
            break;
        }
        printf("failed\n");
    }

    if (!loaded) {
        printf("\nERROR: Could not load umc_audioapi_x64.dll\n");
        return 1;
    }

    // Check API version
    printf("\n--- API Version ---\n");
    if (api.fn.TUSBAUDIO_GetApiVersion) {
        unsigned int ver = api.fn.TUSBAUDIO_GetApiVersion();
        printf("  API version: %d.%d\n", ver >> 16, ver & 0xFFFF);
    }

    // Get driver info
    printf("\n--- Driver Info ---\n");
    if (api.fn.TUSBAUDIO_GetDriverInfo) {
        TUsbAudioDriverInfo info;
        TUsbAudioStatus st = api.fn.TUSBAUDIO_GetDriverInfo(&info);
        if (st == TSTATUS_SUCCESS) {
            printf("  Driver: %d.%d.%d\n", info.driverVersionMajor, info.driverVersionMinor, info.driverVersionSub);
            printf("  API: %d.%d\n", info.apiVersionMajor, info.apiVersionMinor);
        } else {
            printf("  GetDriverInfo failed: 0x%08X\n", st);
        }
    }

    // Enumerate devices
    printf("\n--- Enumerating Devices ---\n");
    TUsbAudioStatus status = api.fn.TUSBAUDIO_EnumerateDevices();
    if (status != TSTATUS_SUCCESS) {
        printf("  EnumerateDevices failed: 0x%08X\n", status);
        return 1;
    }

    unsigned int deviceCount = api.fn.TUSBAUDIO_GetDeviceCount();
    printf("  Found %d device(s)\n", deviceCount);

    for (unsigned int i = 0; i < deviceCount; i++) {
        TUsbAudioHandle handle = nullptr;
        status = api.fn.TUSBAUDIO_OpenDeviceByIndex(i, &handle);
        if (status != TSTATUS_SUCCESS) {
            printf("  Device[%d]: open failed (0x%08X)\n", i, status);
            continue;
        }

        printf("\n--- Device[%d] ---\n", i);

        // Properties
        if (api.fn.TUSBAUDIO_GetDeviceProperties) {
            TUsbAudioDeviceProperties props;
            memset(&props, 0, sizeof(props));
            if (api.fn.TUSBAUDIO_GetDeviceProperties(handle, &props) == TSTATUS_SUCCESS) {
                printf("  VID: 0x%04X  PID: 0x%04X\n", props.usbVendorId, props.usbProductId);
                printf("  Product: %ls\n", props.productString);
                printf("  Manufacturer: %ls\n", props.manufacturerString);
                printf("  Serial: %ls\n", props.serialNumberString);
            }
        }

        // Sample rates
        if (api.fn.TUSBAUDIO_GetSupportedSampleRates) {
            unsigned int rates[32], count = 0;
            if (api.fn.TUSBAUDIO_GetSupportedSampleRates(handle, 32, rates, &count) == TSTATUS_SUCCESS) {
                printf("  Supported rates:");
                for (unsigned int j = 0; j < count; j++) printf(" %d", rates[j]);
                printf("\n");
            }
        }

        if (api.fn.TUSBAUDIO_GetCurrentSampleRate) {
            unsigned int rate = 0;
            if (api.fn.TUSBAUDIO_GetCurrentSampleRate(handle, &rate) == TSTATUS_SUCCESS) {
                printf("  Current rate: %d Hz\n", rate);
            }
        }

        // Channel counts
        if (api.fn.TUSBAUDIO_GetStreamChannelCount) {
            unsigned int inCount = 0, outCount = 0;
            api.fn.TUSBAUDIO_GetStreamChannelCount(handle, 1, &inCount);
            api.fn.TUSBAUDIO_GetStreamChannelCount(handle, 0, &outCount);
            printf("  Channels: %d in, %d out\n", inCount, outCount);
        }

        // Stream formats
        if (api.fn.TUSBAUDIO_GetSupportedStreamFormats) {
            TUsbAudioStreamFormat fmts[16];
            unsigned int fmtCount = 0;

            printf("  Input formats:\n");
            if (api.fn.TUSBAUDIO_GetSupportedStreamFormats(handle, 1, 16, fmts, &fmtCount) == TSTATUS_SUCCESS) {
                for (unsigned int j = 0; j < fmtCount; j++) {
                    printf("    [%d] %d-bit, %d ch: %ls\n", fmts[j].formatId, fmts[j].bitsPerSample, fmts[j].numberOfChannels, fmts[j].formatNameString);
                }
            }

            printf("  Output formats:\n");
            if (api.fn.TUSBAUDIO_GetSupportedStreamFormats(handle, 0, 16, fmts, &fmtCount) == TSTATUS_SUCCESS) {
                for (unsigned int j = 0; j < fmtCount; j++) {
                    printf("    [%d] %d-bit, %d ch: %ls\n", fmts[j].formatId, fmts[j].bitsPerSample, fmts[j].numberOfChannels, fmts[j].formatNameString);
                }
            }
        }

        // Channel properties (names)
        if (api.fn.TUSBAUDIO_GetChannelProperties) {
            TUsbAudioChannelProperty chans[32];
            unsigned int chCount = 0;

            printf("  Input channels:\n");
            if (api.fn.TUSBAUDIO_GetChannelProperties(handle, 1, 32, chans, &chCount) == TSTATUS_SUCCESS) {
                for (unsigned int j = 0; j < chCount; j++) {
                    printf("    [%d] %ls\n", chans[j].channelIndex, chans[j].channelNameString);
                }
            }

            printf("  Output channels:\n");
            if (api.fn.TUSBAUDIO_GetChannelProperties(handle, 0, 32, chans, &chCount) == TSTATUS_SUCCESS) {
                for (unsigned int j = 0; j < chCount; j++) {
                    printf("    [%d] %ls\n", chans[j].channelIndex, chans[j].channelNameString);
                }
            }
        }

        // ASIO buffer sizes
        if (api.fn.TUSBAUDIO_GetAsioBufferSizeSet) {
            unsigned int sizes[64], sizeCount = 0, curIdx = 0;
            if (api.fn.TUSBAUDIO_GetAsioBufferSizeSet(64, sizes, &sizeCount, &curIdx) == TSTATUS_SUCCESS) {
                printf("  ASIO buffer sizes (us):");
                for (unsigned int j = 0; j < sizeCount; j++) {
                    printf("%s %d", j == curIdx ? " [" : " ", sizes[j]);
                    if (j == curIdx) printf("]");
                }
                printf("\n");
            }
        }

        api.fn.TUSBAUDIO_CloseDevice(handle);
    }

    printf("\n=== Test Complete ===\n");
    api.unload();
    return 0;
}
