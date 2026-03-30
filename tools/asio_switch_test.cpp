/*
 * ASIO Buffer Switch Stress Test
 * 
 * 自动化测试：反复切换缓冲区大小，检测每次切换后硬件是否正常出声。
 * 
 * 编译: cl /EHsc /O2 asio_switch_test.cpp /link ole32.lib
 * 运行: asio_switch_test.exe
 */

#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <cstring>
#include <stdint.h>

// 最小化 ASIO 定义
typedef long ASIOError;
typedef long ASIOBool;
typedef double ASIOSampleRate;

#define ASE_OK 0
#define ASIOTrue 1
#define ASIOFalse 0
#define ASIOSTInt32LSB 18
#define ASIOSTFloat32LSB 19

struct ASIOBufferInfo {
    ASIOBool isInput;
    long channelNum;
    void* buffers[2];
};

struct ASIOCallbacks {
    void (*bufferSwitch)(long doubleBufferIndex, ASIOBool directProcess);
    void (*sampleRateDidChange)(ASIOSampleRate sRate);
    long (*asioMessage)(long selector, long value, void* message, double* opt);
    void* (*bufferSwitchTimeInfo)(void* params, long doubleBufferIndex, ASIOBool directProcess);
};

struct ASIOClockSource {
    long index;
    long associatedChannel;
    long associatedGroup;
    ASIOBool isCurrentSource;
    char name[32];
};

struct ASIOChannelInfo {
    long channel;
    ASIOBool isInput;
    ASIOBool isActive;
    long channelGroup;
    long type;
    char name[32];
};

// IASIO interface (COM vtable)
struct IASIO : public IUnknown {
    virtual ASIOBool init(void* sysHandle) = 0;
    virtual void getDriverName(char* name) = 0;
    virtual long getDriverVersion() = 0;
    virtual void getErrorMessage(char* string) = 0;
    virtual ASIOError start() = 0;
    virtual ASIOError stop() = 0;
    virtual ASIOError getChannels(long* numInputChannels, long* numOutputChannels) = 0;
    virtual ASIOError getLatencies(long* inputLatency, long* outputLatency) = 0;
    virtual ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) = 0;
    virtual ASIOError canSampleRate(ASIOSampleRate sampleRate) = 0;
    virtual ASIOError getSampleRate(ASIOSampleRate* sampleRate) = 0;
    virtual ASIOError setSampleRate(ASIOSampleRate sampleRate) = 0;
    virtual ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) = 0;
    virtual ASIOError setClockSource(long reference) = 0;
    virtual ASIOError getSamplePosition(long long* sPos, long long* tStamp) = 0;
    virtual ASIOError getChannelInfo(ASIOChannelInfo* info) = 0;
    virtual ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks) = 0;
    virtual ASIOError disposeBuffers() = 0;
    virtual ASIOError controlPanel() = 0;
    virtual ASIOError future(long selector, void* opt) = 0;
    virtual ASIOError outputReady() = 0;
};

// ========== 全局状态 ==========
static volatile long g_callbackCount = 0;
static volatile bool g_hasNonZeroData = false;
static volatile float g_maxAmplitude = 0.0f;
static long g_bufferSize = 0;
static long g_numInputs = 0;
static long g_numOutputs = 0;
static ASIOBufferInfo* g_bufferInfos = nullptr;

// ASIO Callbacks
void bufferSwitch(long doubleBufferIndex, ASIOBool directProcess) {
    InterlockedIncrement(&g_callbackCount);
    
    // 检查输入通道中是否有非零数据（硬件是否在工作）
    for (long i = 0; i < g_numInputs && i < 2; i++) {
        int32_t* buf = (int32_t*)g_bufferInfos[i].buffers[doubleBufferIndex];
        if (buf) {
            for (long s = 0; s < g_bufferSize; s++) {
                if (buf[s] != 0) {
                    g_hasNonZeroData = true;
                    float amp = fabsf((float)buf[s] / 2147483648.0f);
                    if (amp > g_maxAmplitude) g_maxAmplitude = amp;
                }
            }
        }
    }
    
    // 向输出通道写入 440Hz 正弦波测试音
    static double phase = 0.0;
    double sampleRate = 48000.0;
    double freq = 440.0;
    double phaseInc = 2.0 * 3.14159265358979 * freq / sampleRate;
    
    for (long ch = 0; ch < g_numOutputs && ch < 2; ch++) {
        // Fix: g_bufferInfos only contains usedInputs + usedOutputs elements.
        long idx = 2 + ch; // usedInputs is always 2 in this test.
        int32_t* buf = (int32_t*)g_bufferInfos[idx].buffers[doubleBufferIndex];
        if (buf) {
            double p = phase;
            for (long s = 0; s < g_bufferSize; s++) {
                float sample = (float)sin(p) * 0.3f; // -10dB 440Hz
                buf[s] = (int32_t)(sample * 2147483647.0f);
                p += phaseInc;
            }
        }
    }
    phase += phaseInc * g_bufferSize;
    while (phase > 2.0 * 3.14159265358979) phase -= 2.0 * 3.14159265358979;
}

void sampleRateChanged(ASIOSampleRate sRate) {
    printf("  [EVENT] Sample rate changed to %.0f\n", sRate);
}

long asioMessage(long selector, long value, void* message, double* opt) {
    return 0;
}

// ========== Main ==========
int main() {
    printf("=== UMC Ultra ASIO Buffer Switch 压力测试 ===\n\n");
    
    CoInitialize(nullptr);
    
    // BehringerASIO CLSID
    CLSID clsid;
    CLSIDFromString(L"{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}", &clsid);
    
    IASIO* drv = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, (void**)&drv);
    if (FAILED(hr) || !drv) {
        printf("[FAIL] 无法创建 ASIO 驱动实例 (hr=0x%08lX)\n", hr);
        printf("       请确保 BehringerASIO.dll 已注册 (regsvr32)\n");
        CoUninitialize();
        return 1;
    }
    
    printf("[OK] ASIO 驱动实例已创建\n");
    
    // Init
    if (!drv->init(nullptr)) {
        printf("[FAIL] init() 失败\n");
        drv->Release();
        CoUninitialize();
        return 1;
    }
    
    char drvName[64] = {};
    drv->getDriverName(drvName);
    printf("[OK] 驱动初始化成功: %s (v%ld)\n", drvName, drv->getDriverVersion());
    
    ASIOSampleRate rate;
    drv->getSampleRate(&rate);
    printf("[OK] 当前采样率: %.0f Hz\n", rate);
    
    drv->getChannels(&g_numInputs, &g_numOutputs);
    printf("[OK] 通道: %ld 输入, %ld 输出\n", g_numInputs, g_numOutputs);
    
    long minSize, maxSize, prefSize, granularity;
    drv->getBufferSize(&minSize, &maxSize, &prefSize, &granularity);
    printf("[OK] 缓冲区范围: min=%ld, max=%ld, preferred=%ld\n\n", minSize, maxSize, prefSize);
    
    // 测试的缓冲区大小序列
    long testSizes[] = { 64, 8, 16, 32, 8, 64, 16, 8, 32, 16, 8, 8, 64, 32, 8 };
    int numTests = sizeof(testSizes) / sizeof(testSizes[0]);
    
    int passCount = 0, failCount = 0;
    
    ASIOCallbacks callbacks = {};
    callbacks.bufferSwitch = bufferSwitch;
    callbacks.sampleRateDidChange = sampleRateChanged;
    callbacks.asioMessage = asioMessage;
    
    // 使用前2个输入 + 前2个输出
    long usedInputs = (g_numInputs > 2) ? 2 : g_numInputs;
    long usedOutputs = (g_numOutputs > 2) ? 2 : g_numOutputs;
    long totalCh = usedInputs + usedOutputs;
    
    g_bufferInfos = new ASIOBufferInfo[totalCh];
    
    for (int t = 0; t < numTests; t++) {
        long bufSize = testSizes[t];
        if (bufSize < minSize || bufSize > maxSize) {
            printf("[SKIP] 缓冲区 %ld 超出范围\n", bufSize);
            continue;
        }
        
        printf("--- 测试 %d/%d: 切换到 %ld 采样 ---\n", t + 1, numTests, bufSize);
        g_bufferSize = bufSize;
        
        // 准备 buffer info
        for (long i = 0; i < usedInputs; i++) {
            g_bufferInfos[i].isInput = ASIOTrue;
            g_bufferInfos[i].channelNum = i;
            g_bufferInfos[i].buffers[0] = nullptr;
            g_bufferInfos[i].buffers[1] = nullptr;
        }
        for (long i = 0; i < usedOutputs; i++) {
            g_bufferInfos[usedInputs + i].isInput = ASIOFalse;
            g_bufferInfos[usedInputs + i].channelNum = i;
            g_bufferInfos[usedInputs + i].buffers[0] = nullptr;
            g_bufferInfos[usedInputs + i].buffers[1] = nullptr;
        }
        
        // createBuffers
        ASIOError err = drv->createBuffers(g_bufferInfos, totalCh, bufSize, &callbacks);
        if (err != ASE_OK) {
            printf("  [FAIL] createBuffers 失败 (err=%ld)\n", err);
            failCount++;
            continue;
        }
        
        // 验证缓冲区指针
        bool ptrsOk = true;
        for (long i = 0; i < totalCh; i++) {
            if (!g_bufferInfos[i].buffers[0] || !g_bufferInfos[i].buffers[1]) {
                printf("  [FAIL] 通道 %ld 缓冲区指针为空!\n", i);
                ptrsOk = false;
            }
        }
        
        if (!ptrsOk) {
            drv->disposeBuffers();
            failCount++;
            continue;
        }
        
        // Reset 状态
        g_callbackCount = 0;
        g_hasNonZeroData = false;
        g_maxAmplitude = 0.0f;
        
        // Start
        err = drv->start();
        if (err != ASE_OK) {
            printf("  [FAIL] start() 失败 (err=%ld)\n", err);
            drv->disposeBuffers();
            failCount++;
            continue;
        }
        
        // 等待 1.5 秒收集数据
        Sleep(1500);
        
        long cbCount = g_callbackCount;
        bool hasData = g_hasNonZeroData;
        float maxAmp = g_maxAmplitude;
        
        // 计算预期的回调次数
        double expectedRate = rate / (double)bufSize;
        long expectedCallbacks = (long)(expectedRate * 1.5);
        double actualRate = (double)cbCount / 1.5;
        
        // Stop
        drv->stop();
        drv->disposeBuffers();
        
        // 判定结果
        bool callbackOk = (cbCount > expectedCallbacks * 0.8); // 允许 20% 误差
        
        if (callbackOk) {
            printf("  [OK] 回调: %ld 次 (%.1f Hz, 预期 %.1f Hz)\n", cbCount, actualRate, expectedRate);
            if (hasData) {
                printf("  [OK] 输入有信号 (峰值: %.4f = %.1f dBFS)\n", maxAmp, 20.0f * log10f(maxAmp + 1e-20f));
            } else {
                printf("  [注意] 输入无信号（可能未插入音源，但回调正常）\n");
            }
            passCount++;
        } else {
            printf("  [FAIL] 回调异常: 仅 %ld 次 (预期 ~%ld)\n", cbCount, expectedCallbacks);
            failCount++;
        }
        
        printf("\n");
        
        // 短暂间隔模拟快速切换
        Sleep(200);
    }
    
    printf("========================================\n");
    printf("测试完成: %d 通过, %d 失败 (共 %d)\n", passCount, failCount, passCount + failCount);
    printf("========================================\n");
    
    delete[] g_bufferInfos;
    drv->Release();
    CoUninitialize();
    
    printf("\n测试退出。\n");
    return failCount > 0 ? 1 : 0;
}
