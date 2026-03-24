// Generate a 48kHz 16-bit stereo WAV file with 1kHz sine wave
#include <cstdio>
#include <cstdint>
#include <cmath>

#define PI 3.14159265358979323846

int main() {
    const int sampleRate = 48000;
    const int duration = 60; // 60 seconds
    const int channels = 2;
    const int bitsPerSample = 16;
    const double freq = 1000.0;
    const double amplitude = 0.5;
    
    int totalSamples = sampleRate * duration;
    int dataSize = totalSamples * channels * (bitsPerSample / 8);
    
    const char* filename = "test_sine_1kHz_48k.wav";
    FILE* fp = fopen(filename, "wb");
    if (!fp) { printf("Cannot create %s\n", filename); return 1; }
    
    // WAV header
    uint32_t chunkSize = 36 + dataSize;
    uint16_t audioFormat = 1; // PCM
    uint16_t numChannels = channels;
    uint32_t sr = sampleRate;
    uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
    uint16_t blockAlign = channels * (bitsPerSample / 8);
    uint16_t bps = bitsPerSample;
    
    fwrite("RIFF", 1, 4, fp);
    fwrite(&chunkSize, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    uint32_t fmtSize = 16;
    fwrite(&fmtSize, 4, 1, fp);
    fwrite(&audioFormat, 2, 1, fp);
    fwrite(&numChannels, 2, 1, fp);
    fwrite(&sr, 4, 1, fp);
    fwrite(&byteRate, 4, 1, fp);
    fwrite(&blockAlign, 2, 1, fp);
    fwrite(&bps, 2, 1, fp);
    fwrite("data", 1, 4, fp);
    fwrite(&dataSize, 4, 1, fp);
    
    // Generate sine wave
    for (int i = 0; i < totalSamples; i++) {
        double t = (double)i / sampleRate;
        double val = amplitude * sin(2.0 * PI * freq * t);
        int16_t sample = (int16_t)(val * 32767.0);
        fwrite(&sample, 2, 1, fp); // L
        fwrite(&sample, 2, 1, fp); // R
    }
    
    fclose(fp);
    printf("Generated: %s\n", filename);
    printf("  %d Hz, %d-bit, %d channels, %d seconds\n", sampleRate, bitsPerSample, channels, duration);
    printf("  Sine: %.0f Hz, amplitude %.1f\n", freq, amplitude);
    return 0;
}
