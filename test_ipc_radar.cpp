#include <iostream>
#include <windows.h>
#include <thread>
#include <atomic>
#include <vector>
#include <cmath>
#include "src/bridge/AsmrtopIPC.h"

// Mock the exact IPC memory struct structure needed for testing
IpcAudioBuffer* g_mockBuf = nullptr;

// We inherit exactly from AsmrtopIpcChannel to test its logic flawlessly
class TestIpcChannel : public AsmrtopIpcChannel {
public:
    void injectMockBuffer(IpcAudioBuffer* b) {
        m_buf = b;
    }
    
    // Check internal state
    double exposeDetectedRate() { return getDetectedWdmRate(); }
    bool exposePrefillMode() { return m_prefillMode; }
};

int main() {
    std::cout << "===========================================\n";
    std::cout << " ASMRTOP IPC Auto-Rate Radar & Deadlock Tester\n";
    std::cout << "===========================================\n";

    g_mockBuf = new IpcAudioBuffer();
    g_mockBuf->writePos = 0;
    g_mockBuf->readPos = 0;

    TestIpcChannel ch;
    ch.injectMockBuffer(g_mockBuf);

    // DAW config
    const double dstRate = 48000.0;
    const int numFrames = 1024; // Big buffer -> large srcNeeded at 192kHz

    // Simulation params
    const double simulatedWdmRate = 192000.0;
    
    std::cout << " DAW Buffer: " << numFrames << " frames\n";
    std::cout << " DAW Rate: " << dstRate << " Hz\n";
    std::cout << " Simulating WDM Push Rate: " << simulatedWdmRate << " Hz\n";
    std::cout << " -> srcNeeded will peak at ~ " << (numFrames * (simulatedWdmRate / dstRate)) + 2 << " samples!\n";

    double simulatedTimeSec = 0.0;
    double timeStepSec = (double)numFrames / dstRate; // e.g. 1024 / 48000.0 = 0.02133 sec (21.3 ms)

    uint32_t w = 0;
    std::vector<float> dummyL(numFrames);
    std::vector<float> dummyR(numFrames);

    // Run the DAW loop simulation for 5 seconds virtual time
    for (int step = 0; step < 250; step++) {
        // 1. Advance Virtual Time
        simulatedTimeSec += timeStepSec;

        // 2. Kernel driver pushes data into ring
        w = (uint32_t)(simulatedTimeSec * simulatedWdmRate);
        g_mockBuf->writePos.store(w, std::memory_order_release);

        // 3. DAW consumer pulls data
        ch.readStereoAdaptive(dummyL.data(), dummyR.data(), numFrames, ch.getDetectedWdmRate(), dstRate);

        int32_t available = (int32_t)(w - g_mockBuf->readPos.load());

        std::cout << "Step " << step << " [" << (int)(simulatedTimeSec * 1000) << " ms]: "
                  << "W=" << w << " | R=" << g_mockBuf->readPos.load() 
                  << " | Avail=" << available 
                  << " | Prefill=" << (ch.exposePrefillMode() ? "YES" : "NO")
                  << " | DetectedRate=" << ch.exposeDetectedRate() << "\n";

        // Sleep briefly in real time so QueryPerformanceCounter simulates realistic delta
        Sleep((DWORD)(timeStepSec * 1000.0));
    }

    std::cout << "\nTest Complete.\n";
    delete g_mockBuf;
    return 0;
}
