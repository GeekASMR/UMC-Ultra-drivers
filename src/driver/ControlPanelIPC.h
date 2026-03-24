#pragma once

#include <windows.h>

#define IPC_MAP_NAME L"Local\\UMC_TotalMix_IPC"
#define MAX_SRC 64
#define MAX_DST 64

struct TotalMixIPC {
    bool  initialized; // Set to true by bridge or driver when first created
    float matrixGain[MAX_SRC][MAX_DST];
    bool  matrixMute[MAX_SRC][MAX_DST];
    bool  loopback[MAX_DST];
    float hwInPeaks[MAX_SRC];
    float swPlayPeaks[MAX_DST];
    float hwOutPeaks[MAX_DST];
};

class ControlPanelIPC {
public:
    ControlPanelIPC();
    ~ControlPanelIPC();

    bool init();
    void close();

    TotalMixIPC* get() const { return m_pState; }

private:
    HANDLE m_hMap;
    TotalMixIPC* m_pState;
};
