/*
 * UMCControlProxy.cpp - USB ↔ WDM Virtual Audio Bridge
 * 
 * A headless background service that bridges:
 *   USB Hardware (Behringer UMC via TusbAudioDirect)
 *     ↕ DMA buffers
 *   ASMRTOP WDM Virtual Audio Driver (via shared memory IPC)
 *     ↕ Windows audio stack
 *   All Windows applications
 *
 * Channel mapping (4 stereo pairs):
 *   HW Input  0,1  →  CAP_0 (Virtual Mic 1/2)
 *   HW Input  2,3  →  CAP_1 (Virtual Mic 3/4)
 *   HW Input  4,5  →  CAP_2 (Virtual Mic 5/6)
 *   HW Input  6,7  →  CAP_3 (Virtual Mic 7/8)
 *
 *   PLAY_0 (Virtual Speaker 1/2) →  HW Output 0,1
 *   PLAY_1 (Virtual Speaker 3/4) →  HW Output 2,3
 *   PLAY_2 (Virtual Speaker 5/6) →  HW Output 4,5
 *   PLAY_3 (Virtual Speaker 7/8) →  HW Output 6,7
 */

#include <iostream>
#include <windows.h>
#include <thread>
#include <atomic>
#include <csignal>

#include "../driver/TusbAudioDirect.h"
#include "../driver/AudioBuffer.h"
#include "../bridge/AsmrtopIPC.h"
#include "../utils/Logger.h"

#define LOG_MOD "Bridge"
#define NUM_PAIRS 4
#define MAX_BUF_SIZE 512

static std::atomic<bool> g_running(true);

void signalHandler(int) { g_running.store(false); }

class AudioBridge {
public:
    TusbAudioDirect tusb;
    AsmrtopIpcChannel capChannels[NUM_PAIRS];   // HW input → virtual mic
    AsmrtopIpcChannel playChannels[NUM_PAIRS];  // Virtual speaker → HW output
    
    // Per-channel float buffers (allocated per channel)
    float* inL[NUM_PAIRS];
    float* inR[NUM_PAIRS];
    float* outL[NUM_PAIRS];
    float* outR[NUM_PAIRS];
    
    int bufSize = 0;
    bool streaming = false;

    bool start() {
        LOG_INFO(LOG_MOD, "Initializing USB hardware...");
        if (!tusb.init()) {
            LOG_ERROR(LOG_MOD, "Failed to init TusbAudioDirect");
            return false;
        }
        
        int hwIns = (int)tusb.getInputChannels().size();
        int hwOuts = (int)tusb.getOutputChannels().size();
        LOG_INFO(LOG_MOD, "Hardware: %d inputs, %d outputs", hwIns, hwOuts);
        
        // Use first 8 inputs and first 8 outputs (4 stereo pairs)
        int usableInPairs = (hwIns >= 2) ? (hwIns / 2) : 0;
        int usableOutPairs = (hwOuts >= 2) ? (hwOuts / 2) : 0;
        int pairs = usableInPairs < usableOutPairs ? usableInPairs : usableOutPairs;
        if (pairs > NUM_PAIRS) pairs = NUM_PAIRS;
        LOG_INFO(LOG_MOD, "Using %d stereo pairs", pairs);

        // Create buffers for all channels (need all for Thesycon)
        std::vector<int> inIdx(hwIns), outIdx(hwOuts);
        for (int i = 0; i < hwIns; i++) inIdx[i] = i;
        for (int i = 0; i < hwOuts; i++) outIdx[i] = i;
        
        bufSize = 128;
        if (!tusb.createBuffers(inIdx, outIdx, bufSize)) {
            LOG_ERROR(LOG_MOD, "Failed to create DMA buffers");
            return false;
        }
        
        // Allocate float work buffers
        for (int p = 0; p < NUM_PAIRS; p++) {
            inL[p] = new float[MAX_BUF_SIZE]();
            inR[p] = new float[MAX_BUF_SIZE]();
            outL[p] = new float[MAX_BUF_SIZE]();
            outR[p] = new float[MAX_BUF_SIZE]();
        }

        // Open IPC shared memory sections
        LOG_INFO(LOG_MOD, "Opening IPC shared memory...");
        int capOk = 0, playOk = 0;
        for (int p = 0; p < NUM_PAIRS; p++) {
            if (capChannels[p].open("REC", p)) capOk++;
            if (playChannels[p].open("PLAY", p)) playOk++;
        }
        LOG_INFO(LOG_MOD, "IPC: %d/%d CAP channels, %d/%d PLAY channels", capOk, NUM_PAIRS, playOk, NUM_PAIRS);
        
        if (capOk == 0 && playOk == 0) {
            LOG_ERROR(LOG_MOD, "No IPC channels available! Is the virtual audio driver installed?");
            return false;
        }

        // Start streaming
        tusb.setBufferSwitchCallback(bufferSwitchStatic, this);
        if (!tusb.start()) {
            LOG_ERROR(LOG_MOD, "Failed to start streaming");
            return false;
        }
        
        streaming = true;
        LOG_INFO(LOG_MOD, "=== Bridge Active: USB ↔ Virtual Audio ===");
        return true;
    }

    void stop() {
        if (!streaming) return;
        tusb.stop();
        tusb.disposeBuffers();
        tusb.close();
        
        for (int p = 0; p < NUM_PAIRS; p++) {
            capChannels[p].close();
            playChannels[p].close();
            delete[] inL[p]; delete[] inR[p];
            delete[] outL[p]; delete[] outR[p];
        }
        streaming = false;
        LOG_INFO(LOG_MOD, "Bridge stopped");
    }

    static void bufferSwitchStatic(long bufIdx, void* ctx) {
        ((AudioBridge*)ctx)->onBufferSwitch(bufIdx);
    }

    void onBufferSwitch(long bufIdx) {
        // === HW Input → Virtual Mic (CAP) ===
        for (int p = 0; p < NUM_PAIRS; p++) {
            int chL = p * 2;
            int chR = p * 2 + 1;
            
            // Read DMA input buffers → convert to float
            const int* dmaL = (const int*)tusb.getChannelBuffer(true, chL, bufIdx);
            const int* dmaR = (const int*)tusb.getChannelBuffer(true, chR, bufIdx);
            
            if (dmaL) AudioBuffer::convertInt32ToFloat32(dmaL, inL[p], bufSize);
            else memset(inL[p], 0, bufSize * sizeof(float));
            
            if (dmaR) AudioBuffer::convertInt32ToFloat32(dmaR, inR[p], bufSize);
            else memset(inR[p], 0, bufSize * sizeof(float));
            
            // Write to virtual mic ring buffer
            if (capChannels[p].isOpen()) {
                // USB Hardware clock to WDM clock (nominally 48k to 48k)
                capChannels[p].writeStereoSRC(inL[p], inR[p], bufSize, 48000.0, 48000.0);
            }
        }

        // === Virtual Speaker (PLAY) → HW Output ===
        for (int p = 0; p < NUM_PAIRS; p++) {
            int chL = p * 2;
            int chR = p * 2 + 1;
            
            // Read from virtual speaker ring buffer
            if (playChannels[p].isOpen()) {
                playChannels[p].readStereoAdaptive(outL[p], outR[p], bufSize, 48000.0, 48000.0);
            } else {
                memset(outL[p], 0, bufSize * sizeof(float));
                memset(outR[p], 0, bufSize * sizeof(float));
            }
            
            // Convert float → write to DMA output buffers
            int* dmaOutL = (int*)tusb.getChannelBuffer(false, chL, bufIdx);
            int* dmaOutR = (int*)tusb.getChannelBuffer(false, chR, bufIdx);
            
            if (dmaOutL) AudioBuffer::convertFloat32ToInt32(outL[p], dmaOutL, bufSize);
            if (dmaOutR) AudioBuffer::convertFloat32ToInt32(outR[p], dmaOutR, bufSize);
        }
    }
};

int main() {
    Logger::getInstance().init(nullptr);  // Default: %TEMP%\BehringerASIO.log
    LOG_INFO(LOG_MOD, "UMC Audio Bridge v1.0 starting...");
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    AudioBridge bridge;
    
    if (!bridge.start()) {
        LOG_ERROR(LOG_MOD, "Bridge failed to start. Exiting.");
        std::cerr << "ERROR: Bridge failed to start. Check log.\n";
        return 1;
    }
    
    std::cout << "UMC Audio Bridge running. Press Ctrl+C to stop.\n";
    
    while (g_running.load()) {
        Sleep(500);
    }
    
    bridge.stop();
    LOG_INFO(LOG_MOD, "Bridge exited cleanly.");
    return 0;
}
