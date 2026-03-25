/*
 * usb_stream_test.cpp - WinUSB/libusb等时传输播放测试
 * 
 * 作用: 不依赖任何系统音频驱动，直接通过 USB 发送正弦波到 UMC 1820
 * 编译: cl /EHsc /O2 /I../src /I../libusb/include usb_stream_test.cpp ../src/usb/UsbAudioDevice.cpp /link ../libusb/VS2022/MS64/dll/libusb-1.0.lib
 */

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <thread>
#include <atomic>
#include "libusb.h"
#include "usb/UsbAudioDevice.h"

#define UMC_VID 0x1397
#define UMC_PID 0x0503
#define NUM_ISO_TRANSFERS 8
#define PACKETS_PER_TRANSFER 8

// 400 Hz 正弦波生成器
const double PI = 3.14159265358979323846;
double phase = 0;
double freq = 400.0;
double sampleRate = 48000.0;

std::atomic<bool> g_running(true);

void LIBUSB_CALL outCallback(struct libusb_transfer* transfer) {
    if (!g_running) return;

    // 填充音频包 (UAC2 OUT: Interface 1, AltSetting 1, 20 Channels, 24-bit in 3 bytes)
    for (int i = 0; i < transfer->num_iso_packets; i++) {
        struct libusb_iso_packet_descriptor* packet = &transfer->iso_packet_desc[i];
        
        // 48kHz / 8000 microframes = 6 samples per microframe
        int numSamples = 6;
        int numChannels = 20;
        int bytesPerSample = 3; 
        int frameSize = numChannels * bytesPerSample;
        int payloadLen = numSamples * frameSize;
        
        uint8_t* payload = libusb_get_iso_packet_buffer_simple(transfer, i);
        
        for (int s = 0; s < numSamples; s++) {
            double sineVal = sin(phase * 2 * PI);
            phase += freq / sampleRate;
            if (phase >= 1.0) phase -= 1.0;

            // 转换成 24-bit PCM (缩放幅度 0.5)
            int32_t pcmVal = (int32_t)(sineVal * 0.5 * 8388607.0);

            for (int ch = 0; ch < numChannels; ch++) {
                int offset = s * frameSize + ch * bytesPerSample;
                
                // 只在 OUT 1 和 2 放送正弦波
                if (ch == 0 || ch == 1) {
                    payload[offset + 0] = (pcmVal & 0xFF);
                    payload[offset + 1] = ((pcmVal >> 8) & 0xFF);
                    payload[offset + 2] = ((pcmVal >> 16) & 0xFF);
                } else {
                    payload[offset + 0] = 0;
                    payload[offset + 1] = 0;
                    payload[offset + 2] = 0;
                }
            }
        }
    }
    
    int rc = libusb_submit_transfer(transfer);
    if (rc < 0) {
        printf("Re-submit failed: %s\n", libusb_error_name(rc));
    }
}

int main() {
    printf("===============================================\n");
    printf("  WinUSB Direct Isochronous Streaming Test\n");
    printf("  Sending 400Hz Sine wave to OUT 1/2...\n");
    printf("===============================================\n");

    UsbAudioDevice dev;
    if (!dev.open(UMC_VID, UMC_PID)) {
        printf("Failed to open UMC 1820. Make sure WinUSB driver is installed via Zadig!\n");
        return 1;
    }

    // Set Sample Rate to 48000
    if (dev.setSampleRate(48000)) {
        printf("Set sample rate to 48000Hz\n");
    }

    const auto& outs = dev.getRenderEndpoints();
    if (outs.empty()) {
        printf("No OUT endpoints found.\n");
        return 1;
    }

    auto outEp = outs[0];
    printf("Found OUT Endpoint: Address=0x%02X, MaxPacket=%d, Channels=%d\n", 
           outEp.address, outEp.maxPacketSize, outEp.numChannels);

    // 激活 AltSetting 1 开始音频流
    if (!dev.setInterfaceAltSetting(outEp.interfaceNum, outEp.altSetting)) {
        printf("Failed to set AltSetting %d on Interface %d\n", outEp.altSetting, outEp.interfaceNum);
        return 1;
    }
    printf("Activated OUT Interface %d AltSetting %d\n", outEp.interfaceNum, outEp.altSetting);

    // 分配传输
    std::vector<libusb_transfer*> transfers;
    std::vector<uint8_t*> buffers;

    int outPacketSize = 360; // 6 samples per microframe at 48kHz

    for (int i = 0; i < NUM_ISO_TRANSFERS; i++) {
        uint8_t* buf = new uint8_t[outPacketSize * PACKETS_PER_TRANSFER]();
        buffers.push_back(buf);

        struct libusb_transfer* transfer = libusb_alloc_transfer(PACKETS_PER_TRANSFER);
        libusb_fill_iso_transfer(transfer, dev.getHandle(), outEp.address,
            buf, outPacketSize * PACKETS_PER_TRANSFER, PACKETS_PER_TRANSFER,
            outCallback, NULL, 0);
        
        libusb_set_iso_packet_lengths(transfer, outPacketSize);
        transfers.push_back(transfer);
        
        // 挂载第一轮的数据
        outCallback(transfer); 
    }

    printf("Streaming started. Press ENTER to stop...\n");

    // 轮询线程
    std::thread pollThread([]() {
        timeval tv = { 0, 1000 };
        while (g_running) {
            libusb_handle_events_timeout_completed(NULL, &tv, NULL);
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(10));
    g_running = false;

    // 清理
    pollThread.join();

    for (auto* tx : transfers) {
        libusb_cancel_transfer(tx);
        libusb_free_transfer(tx);
    }
    for (auto* buf : buffers) {
        delete[] buf;
    }

    dev.setInterfaceAltSetting(outEp.interfaceNum, 0); // 停止流
    dev.close();

    printf("Done.\n");
    return 0;
}
