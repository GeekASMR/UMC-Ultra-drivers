/*
 * RoutingMatrix - TotalMix-style routing matrix
 * 
 * Supports virtual channels inspired by MIDIPLUS Studio driver design:
 *   - Physical channels (Virtual=0): direct hardware I/O
 *   - Virtual channels (Virtual=1): software PLAYBACK / VIRTUAL REC
 */

#pragma once

#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <string>

// A single routing crosspoint in the NxM matrix
struct RoutingCrossPoint {
    float gain;
    float pan;
    bool  enabled;
    bool  mute;

    RoutingCrossPoint() : gain(1.0f), pan(0.0f), enabled(false), mute(false) {}
};

// ASIO channel descriptor (inspired by MIDIPLUS AsioProfiles)
struct AsioChannelDesc {
    int    channelIndex;      // ASIO channel index
    int    hwChannelIndex;    // Mapped hardware channel (-1 if virtual-only)
    bool   isVirtual;         // false=physical (Virtual=0), true=virtual (Virtual=1)
    bool   isInput;           // true=input/capture, false=output/render
    std::string name;         // Channel name (e.g., "Analog In 1", "PLAYBACK 1")
};

// Channel layout descriptor
struct ChannelLayout {
    int hwInputs;             // Physical hardware input channels
    int hwOutputs;            // Physical hardware output channels
    int virtualOutputs;       // Virtual playback channels (PLAYBACK 1~N)
    int virtualInputs;        // Virtual recording channels (VIRTUAL REC 1~N, Loopback)
    int loopbackInputs;       // Loopback channels (subset of virtualInputs, mirrors hwOutputs)
    int digitalInputs;         // Digital I/O input channels: SPDIF(2) or ADAT(4/8)
    int digitalOutputs;        // Digital I/O output channels: SPDIF(2) or ADAT(4/8)

    int totalAsioInputs()  const { return hwInputs + virtualInputs; }
    int totalAsioOutputs() const { return hwOutputs + virtualOutputs; }
    int totalSources()     const { return hwInputs + totalAsioOutputs(); }
    int totalDests()       const { return hwOutputs + virtualOutputs; }
};

class RoutingMatrix {
public:
    RoutingMatrix();
    ~RoutingMatrix();

    void init(int hwInputs, int hwOutputs, int virtualOutputPairs = 1,
              int digitalInputs = 0, int digitalOutputs = 0);

    const ChannelLayout& getLayout() const { return m_layout; }

    // --- Source/Destination index helpers ---
    int hwInputSourceIndex(int ch)    const { return ch; }
    int swPlaybackSourceIndex(int ch) const { return m_layout.hwInputs + ch; }
    int hwOutputDestIndex(int ch)      const { return ch; }
    int virtualOutputDestIndex(int ch) const { return m_layout.hwOutputs + ch; }

    // --- Channel descriptors for ASIO ---
    const std::vector<AsioChannelDesc>& getInputDescs()  const { return m_inputDescs; }
    const std::vector<AsioChannelDesc>& getOutputDescs() const { return m_outputDescs; }

    RoutingCrossPoint& at(int srcIdx, int dstIdx);
    const RoutingCrossPoint& at(int srcIdx, int dstIdx) const;

    void setDefaultRouting();
    void enableDirectMonitoring(bool enable);

    void process(
        const float* const* hwInputBufs,  int numHwIn,
        const float* const* swPlayBufs,   int numSwPlay,
        float**             hwOutputBufs, int numHwOut,
        float**             loopbackBufs, int numLoopback,
        int bufferSize);

    static float dbToLinear(float db);
    static float linearToDb(float linear);

private:
    ChannelLayout m_layout;
    std::vector<std::vector<RoutingCrossPoint>> m_matrix;
    std::vector<std::vector<float>> m_mixBufs;
    int m_mixBufSize;

    // Channel descriptors
    std::vector<AsioChannelDesc> m_inputDescs;
    std::vector<AsioChannelDesc> m_outputDescs;

    void buildChannelDescs();
    float calcPanGain(float pan, int channel) const;
    void ensureMixBuffers(int size);
};
