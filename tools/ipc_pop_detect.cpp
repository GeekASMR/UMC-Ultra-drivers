// Pop/click detector - reads IPC ring and detects sample discontinuities
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <cstdint>

struct IpcAudioBuffer {
    volatile uint32_t writePos;
    volatile uint32_t readPos;
    float ringL[131072];
    float ringR[131072];
};

int main() {
    printf("=== IPC Pop/Click Detector ===\n");
    printf("Play audio to Virtual 1/2 (DirectSound) to detect pops.\n\n");

    // Try to open PLAY_0
    HANDLE hMap = NULL;
    IpcAudioBuffer* buf = nullptr;
    
    while (!buf) {
        hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, "Global\\VirtualAudioWDM_PLAY_0");
        if (hMap) {
            buf = (IpcAudioBuffer*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(IpcAudioBuffer));
            if (buf) break;
            CloseHandle(hMap);
            hMap = NULL;
        }
        printf("Waiting for PLAY_0 shared memory...\r");
        Sleep(500);
    }
    printf("Connected to VirtualAudioWDM_PLAY_0\n\n");

    uint32_t lastW = buf->writePos;
    float prevL = 0.0f, prevR = 0.0f;
    int popCount = 0;
    int totalFrames = 0;
    int silentFrames = 0;
    
    // Threshold for detecting a pop: sudden sample jump
    const float POP_THRESHOLD = 0.15f;  // ~15% full scale jump in one sample

    printf("Monitoring... Press Ctrl+C to stop.\n");
    printf("%-8s %-10s %-10s %-12s %-10s %-10s\n", 
           "Time", "WriteRate", "Pops", "MaxJump_L", "MaxJump_R", "Status");

    LARGE_INTEGER qpcFreq, qpcStart, qpcNow;
    QueryPerformanceFrequency(&qpcFreq);
    QueryPerformanceCounter(&qpcStart);

    float maxJumpL = 0.0f, maxJumpR = 0.0f;
    int popsThisSec = 0;
    int framesThisSec = 0;
    double lastReportTime = 0.0;

    // Skip to current position
    uint32_t readPos = buf->writePos;
    prevL = 0.0f;
    prevR = 0.0f;

    while (true) {
        uint32_t w = buf->writePos;
        int32_t newFrames = (int32_t)(w - readPos);
        
        if (newFrames < 0 || newFrames > 131072) {
            readPos = w;
            newFrames = 0;
        }

        // Process new frames
        for (int32_t i = 0; i < newFrames && i < 8192; i++) {
            uint32_t idx = readPos & 131071;
            float sL = buf->ringL[idx];
            float sR = buf->ringR[idx];
            
            // Skip silence (no point checking jumps in silence)
            bool isSilent = (fabsf(sL) < 1e-6f && fabsf(sR) < 1e-6f);
            bool wasSilent = (fabsf(prevL) < 1e-6f && fabsf(prevR) < 1e-6f);
            
            if (!isSilent && !wasSilent) {
                float jumpL = fabsf(sL - prevL);
                float jumpR = fabsf(sR - prevR);
                
                if (jumpL > maxJumpL) maxJumpL = jumpL;
                if (jumpR > maxJumpR) maxJumpR = jumpR;
                
                if (jumpL > POP_THRESHOLD || jumpR > POP_THRESHOLD) {
                    popsThisSec++;
                    popCount++;
                }
            }
            
            if (isSilent) silentFrames++;
            
            prevL = sL;
            prevR = sR;
            readPos++;
            framesThisSec++;
            totalFrames++;
        }

        // Catch up if too far behind
        if (newFrames > 8192) {
            readPos = w;
        }

        // Report every second
        QueryPerformanceCounter(&qpcNow);
        double elapsed = (double)(qpcNow.QuadPart - qpcStart.QuadPart) / qpcFreq.QuadPart;
        
        if (elapsed - lastReportTime >= 1.0) {
            const char* status = framesThisSec > 0 ? 
                (popsThisSec > 0 ? "!! POPS !!" : "CLEAN") : "SILENT";
            
            printf("t=%-5.0f  %-10d %-10d %-12.4f %-10.4f %s\n",
                   elapsed, framesThisSec, popsThisSec, maxJumpL, maxJumpR, status);
            
            maxJumpL = 0.0f;
            maxJumpR = 0.0f;
            popsThisSec = 0;
            framesThisSec = 0;
            lastReportTime = elapsed;
        }

        Sleep(1);  // 1ms polling
    }

    return 0;
}
