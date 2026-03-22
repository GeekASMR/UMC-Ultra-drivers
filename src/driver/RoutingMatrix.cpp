/*
 * RoutingMatrix implementation
 */

#include "RoutingMatrix.h"
#include "../utils/Logger.h"

#define LOG_MODULE "RoutingMatrix"

static constexpr float PI_HALF = 1.5707963267948966f;

RoutingMatrix::RoutingMatrix() : m_mixBufSize(0) {
    memset(&m_layout, 0, sizeof(m_layout));
}

RoutingMatrix::~RoutingMatrix() {}

void RoutingMatrix::init(int hwInputs, int hwOutputs, int virtualOutputPairs,
                         int digitalInputs, int digitalOutputs) {
    m_layout.hwInputs       = hwInputs;
    m_layout.hwOutputs      = hwOutputs;
    m_layout.virtualOutputs = virtualOutputPairs * 2;
    m_layout.digitalInputs  = digitalInputs;
    m_layout.digitalOutputs = digitalOutputs;

    // No loopback needed
    m_layout.loopbackInputs = 0;
    m_layout.virtualInputs  = m_layout.virtualOutputs;  // Only virtual rec channels

    int nSrc = m_layout.totalSources();
    int nDst = m_layout.totalDests();

    m_matrix.assign(nSrc, std::vector<RoutingCrossPoint>(nDst));

    LOG_INFO(LOG_MODULE, "Matrix %dx%d | HW-In:%d HW-Out:%d(Digital:%d) Virt-Out:%d Virt-In:%d Loopback:%d",
             nSrc, nDst, hwInputs, hwOutputs, digitalOutputs,
             m_layout.virtualOutputs, m_layout.virtualInputs, m_layout.loopbackInputs);
    LOG_INFO(LOG_MODULE, "ASIO totals: %d inputs, %d outputs",
             m_layout.totalAsioInputs(), m_layout.totalAsioOutputs());

    buildChannelDescs();
    setDefaultRouting();
}

RoutingCrossPoint& RoutingMatrix::at(int s, int d) { return m_matrix[s][d]; }
const RoutingCrossPoint& RoutingMatrix::at(int s, int d) const { return m_matrix[s][d]; }

void RoutingMatrix::setDefaultRouting() {
    for (auto& row : m_matrix)
        for (auto& cp : row) {
            cp.enabled = false; cp.gain = 1.0f;
            cp.pan = 0.0f;     cp.mute = false;
        }

    // SW Playback 0..hwOut-1 -> HW Output 0..hwOut-1 (DAW main -> speakers)
    for (int i = 0; i < m_layout.hwOutputs; i++) {
        auto& cp = at(swPlaybackSourceIndex(i), hwOutputDestIndex(i));
        cp.enabled = true;
        cp.gain = 1.0f;
    }

    // SW Playback hwOut..hwOut+virtOut-1 -> Virtual Output 0..virtOut-1
    for (int i = 0; i < m_layout.virtualOutputs; i++) {
        auto& cp = at(swPlaybackSourceIndex(m_layout.hwOutputs + i),
                       virtualOutputDestIndex(i));
        cp.enabled = true;
        cp.gain = 1.0f;
    }

    LOG_INFO(LOG_MODULE, "Default routing applied");
}

void RoutingMatrix::enableDirectMonitoring(bool enable) {
    int pairs = (std::min)(m_layout.hwInputs, m_layout.hwOutputs);
    for (int i = 0; i < pairs; i++)
        at(hwInputSourceIndex(i), hwOutputDestIndex(i)).enabled = enable;
    LOG_INFO(LOG_MODULE, "Direct monitoring %s", enable ? "ON" : "OFF");
}

void RoutingMatrix::ensureMixBuffers(int size) {
    int nDst = m_layout.totalDests();
    if (m_mixBufSize >= size && (int)m_mixBufs.size() >= nDst) return;
    m_mixBufs.resize(nDst);
    for (auto& b : m_mixBufs) b.resize(size);
    m_mixBufSize = size;
}

float RoutingMatrix::calcPanGain(float pan, int channel) const {
    float angle = (pan + 1.0f) * 0.5f;
    return (channel == 0) ? cosf(angle * PI_HALF) : sinf(angle * PI_HALF);
}

void RoutingMatrix::process(
    const float* const* hwInputBufs,  int numHwIn,
    const float* const* swPlayBufs,   int numSwPlay,
    float**             hwOutputBufs, int numHwOut,
    float**             loopbackBufs, int numLoopback,
    int bufferSize)
{
    ensureMixBuffers(bufferSize);

    int nSrc = m_layout.totalSources();
    int nDst = m_layout.totalDests();

    // Clear mix accumulators
    for (int d = 0; d < nDst; d++)
        memset(m_mixBufs[d].data(), 0, bufferSize * sizeof(float));

    // Accumulate sources into destinations
    for (int s = 0; s < nSrc; s++) {
        const float* src = nullptr;
        if (s < m_layout.hwInputs) {
            if (s < numHwIn) src = hwInputBufs[s];
        } else {
            int pi = s - m_layout.hwInputs;
            if (pi < numSwPlay) src = swPlayBufs[pi];
        }
        if (!src) continue;

        for (int d = 0; d < nDst; d++) {
            const RoutingCrossPoint& cp = m_matrix[s][d];
            if (!cp.enabled || cp.mute) continue;

            float g = cp.gain * calcPanGain(cp.pan, d & 1);
            if (g < 1e-5f) continue;

            float* dst = m_mixBufs[d].data();
            for (int i = 0; i < bufferSize; i++)
                dst[i] += src[i] * g;
        }
    }

    // Copy to HW output
    for (int i = 0; i < numHwOut && i < m_layout.hwOutputs; i++)
        memcpy(hwOutputBufs[i], m_mixBufs[i].data(), bufferSize * sizeof(float));

    // Copy to loopback (HW out mix first, then virtual out mix)
    for (int i = 0; i < numLoopback && i < nDst; i++)
        memcpy(loopbackBufs[i], m_mixBufs[i].data(), bufferSize * sizeof(float));
}

float RoutingMatrix::dbToLinear(float db) {
    return powf(10.0f, db / 20.0f);
}

float RoutingMatrix::linearToDb(float linear) {
    return 20.0f * log10f(linear > 1e-4f ? linear : 1e-4f);
}

void RoutingMatrix::buildChannelDescs() {
    m_outputDescs.clear();
    m_inputDescs.clear();

    int ch = 0;

    // === OUTPUT DESCRIPTORS ===
    // Physical analog outputs
    int analogOuts = m_layout.hwOutputs - m_layout.digitalOutputs;
    for (int i = 0; i < analogOuts; i++) {
        AsioChannelDesc desc;
        desc.channelIndex = ch;
        desc.hwChannelIndex = i;
        desc.isVirtual = false;
        desc.isInput = false;
        char name[32];
        snprintf(name, sizeof(name), "OUT %02d", i + 1);
        desc.name = name;
        m_outputDescs.push_back(desc);
        ch++;
    }
    // Physical digital outputs (SPDIF or ADAT depending on hardware switch)
    for (int i = 0; i < m_layout.digitalOutputs; i++) {
        AsioChannelDesc desc;
        desc.channelIndex = ch;
        desc.hwChannelIndex = analogOuts + i;
        desc.isVirtual = false;
        desc.isInput = false;
        char name[32];
        snprintf(name, sizeof(name), "SPDIF OUT %d", i + 1);
        desc.name = name;
        m_outputDescs.push_back(desc);
        ch++;
    }
    // Virtual playback outputs
    for (int i = 0; i < m_layout.virtualOutputs; i++) {
        AsioChannelDesc desc;
        desc.channelIndex = ch;
        desc.hwChannelIndex = -1;  // No direct HW mapping
        desc.isVirtual = true;
        desc.isInput = false;
        desc.name = "PLAYBACK " + std::to_string(i + 1);
        m_outputDescs.push_back(desc);
        ch++;
    }

    // === INPUT DESCRIPTORS ===
    ch = 0;
    // Physical analog inputs
    int analogIns = m_layout.hwInputs - m_layout.digitalInputs;
    for (int i = 0; i < analogIns; i++) {
        AsioChannelDesc desc;
        desc.channelIndex = ch;
        desc.hwChannelIndex = i;
        desc.isVirtual = false;
        desc.isInput = true;
        char name[32];
        snprintf(name, sizeof(name), "IN %02d", i + 1);
        desc.name = name;
        m_inputDescs.push_back(desc);
        ch++;
    }
    // Physical digital inputs (SPDIF or ADAT depending on hardware switch)
    for (int i = 0; i < m_layout.digitalInputs; i++) {
        AsioChannelDesc desc;
        desc.channelIndex = ch;
        desc.hwChannelIndex = analogIns + i;
        desc.isVirtual = false;
        desc.isInput = true;
        char name[32];
        snprintf(name, sizeof(name), "SPDIF IN %d", i + 1);
        desc.name = name;
        m_inputDescs.push_back(desc);
        ch++;
    }
    // (Loopback channels removed)
    // Virtual recording inputs
    for (int i = 0; i < m_layout.virtualOutputs; i++) {
        AsioChannelDesc desc;
        desc.channelIndex = ch;
        desc.hwChannelIndex = -1;
        desc.isVirtual = true;
        desc.isInput = true;
        desc.name = "VIRTUAL REC " + std::to_string(i + 1);
        m_inputDescs.push_back(desc);
        ch++;
    }

    LOG_INFO(LOG_MODULE, "Built %d output + %d input channel descriptors",
             (int)m_outputDescs.size(), (int)m_inputDescs.size());
}
