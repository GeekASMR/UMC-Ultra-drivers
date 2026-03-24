// DirectSound Sine Wave Test - plays 1kHz sine directly via DirectSound API
// to the first "Virtual" audio device found
#define _USE_MATH_DEFINES
#include <windows.h>
#include <mmdeviceapi.h>
#include <dsound.h>
#include <cstdio>
#include <cmath>
#include <cstring>

#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "ole32.lib")

// Find Virtual Audio device GUID
static GUID g_virtualDeviceGuid = GUID_NULL;
static BOOL CALLBACK DSEnumCallback(LPGUID lpGuid, LPCSTR lpcstrDescription, LPCSTR lpcstrModule, LPVOID lpContext)
{
    if (lpGuid && lpcstrDescription && strstr(lpcstrDescription, "Virtual")) {
        g_virtualDeviceGuid = *lpGuid;
        printf("  -> Selected: %s\n", lpcstrDescription);
        return FALSE; // stop enumeration
    }
    if (lpcstrDescription) {
        printf("  Device: %s\n", lpcstrDescription);
    }
    return TRUE;
}

int main()
{
    printf("=== DirectSound Sine Wave Test ===\n");
    printf("Plays 1kHz sine wave directly via DirectSound API\n\n");

    CoInitialize(NULL);

    // Enumerate devices
    printf("Available DirectSound devices:\n");
    DirectSoundEnumerateA(DSEnumCallback, NULL);

    if (g_virtualDeviceGuid == GUID_NULL) {
        printf("\nERROR: No 'Virtual' audio device found!\n");
        return 1;
    }

    // Create DirectSound
    LPDIRECTSOUND8 pDS = NULL;
    HRESULT hr = DirectSoundCreate8(&g_virtualDeviceGuid, &pDS, NULL);
    if (FAILED(hr)) {
        printf("DirectSoundCreate8 failed: 0x%08X\n", hr);
        return 1;
    }

    // Need a window handle for cooperative level
    HWND hWnd = GetDesktopWindow();
    hr = pDS->SetCooperativeLevel(hWnd, DSSCL_PRIORITY);
    if (FAILED(hr)) {
        printf("SetCooperativeLevel failed: 0x%08X\n", hr);
        pDS->Release();
        return 1;
    }

    // Setup format: 16-bit stereo 48kHz
    const DWORD SAMPLE_RATE = 48000;
    const WORD CHANNELS = 2;
    const WORD BITS = 16;
    const WORD BLOCK_ALIGN = CHANNELS * BITS / 8;
    const DWORD BUFFER_SECONDS = 2;
    const DWORD BUFFER_SIZE = SAMPLE_RATE * BLOCK_ALIGN * BUFFER_SECONDS;

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = CHANNELS;
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = BITS;
    wfx.nBlockAlign = BLOCK_ALIGN;
    wfx.nAvgBytesPerSec = SAMPLE_RATE * BLOCK_ALIGN;

    // Create secondary buffer
    DSBUFFERDESC dsbd = {};
    dsbd.dwSize = sizeof(DSBUFFERDESC);
    dsbd.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2;
    dsbd.dwBufferBytes = BUFFER_SIZE;
    dsbd.lpwfxFormat = &wfx;

    LPDIRECTSOUNDBUFFER pDSB = NULL;
    hr = pDS->CreateSoundBuffer(&dsbd, &pDSB, NULL);
    if (FAILED(hr)) {
        printf("CreateSoundBuffer failed: 0x%08X\n", hr);
        pDS->Release();
        return 1;
    }

    // Fill buffer with 1kHz sine wave
    LPVOID pBuf1 = NULL, pBuf2 = NULL;
    DWORD size1 = 0, size2 = 0;
    hr = pDSB->Lock(0, BUFFER_SIZE, &pBuf1, &size1, &pBuf2, &size2, 0);
    if (SUCCEEDED(hr)) {
        SHORT* samples = (SHORT*)pBuf1;
        DWORD totalSamples = size1 / sizeof(SHORT);
        double freq = 1000.0;
        double amplitude = 16000.0; // ~50% volume
        
        for (DWORD i = 0; i < totalSamples; i += 2) {
            double t = (double)(i / 2) / SAMPLE_RATE;
            SHORT val = (SHORT)(amplitude * sin(2.0 * M_PI * freq * t));
            samples[i] = val;      // L
            samples[i + 1] = val;  // R
        }
        pDSB->Unlock(pBuf1, size1, pBuf2, size2);
    }

    // Play looping
    printf("\nPlaying 1kHz sine via DirectSound to Virtual device...\n");
    printf("Press Enter to stop.\n\n");
    
    hr = pDSB->Play(0, 0, DSBPLAY_LOOPING);
    if (FAILED(hr)) {
        printf("Play failed: 0x%08X\n", hr);
    } else {
        printf("Playing... listen for pops/clicks.\n");
        getchar();
    }

    pDSB->Stop();
    pDSB->Release();
    pDS->Release();
    CoUninitialize();

    printf("Done.\n");
    return 0;
}
