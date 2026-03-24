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

void RoutingMatrix::init(int hwInputs, int hwOutputs, int digitalInputs, int digitalOutputs) {
    m_layout.hwInputs       = hwInputs;
    m_layout.hwOutputs      = hwOutputs;
    m_layout.digitalInputs  = digitalInputs;
    m_layout.digitalOutputs = digitalOutputs;

    int nSrc = m_layout.totalSources();
    int nDst = m_layout.totalDests();

    m_matrix.assign(nSrc, std::vector<RoutingCrossPoint>(nDst));
    m_loopback.assign(m_layout.hwOutputs, false);

    // Hardcode OUT 1/2 Loopback to ON for immediate user demonstration purposes
    if (m_layout.hwOutputs > 1) {
        m_loopback[0] = true;
        m_loopback[1] = true;
    }

    LOG_INFO(LOG_MODULE, "Matrix %dx%d | HW-In:%d HW-Out:%d (RME Mode)",
             nSrc, nDst, hwInputs, hwOutputs);
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

    LOG_INFO(LOG_MODULE, "Default routing applied");
}

void RoutingMatrix::enableDirectMonitoring(bool enable) {
    int pairs = (std::min)(m_layout.hwInputs, m_layout.hwOutputs);
    for (int i = 0; i < pairs; i++)
        at(hwInputSourceIndex(i), hwOutputDestIndex(i)).enabled = enable;
    LOG_INFO(LOG_MODULE, "Direct monitoring %s", enable ? "ON" : "OFF");
}

void RoutingMatrix::setLoopback(int hwOutputChannel, bool enable) {
    if (hwOutputChannel >= 0 && hwOutputChannel < m_layout.hwOutputs) {
        m_loopback[hwOutputChannel] = enable;
        LOG_INFO(LOG_MODULE, "Loopback for OUT %d is now %s", hwOutputChannel + 1, enable ? "ON" : "OFF");
    }
}

bool RoutingMatrix::isLoopbackActive(int hwOutputChannel) const {
    if (hwOutputChannel >= 0 && hwOutputChannel < m_layout.hwOutputs) {
        return m_loopback[hwOutputChannel];
    }
    return false;
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

    // Copy to loopback targets (destinations mapped directly to certain ASIO inputs)
    for (int i = 0; i < numLoopback && i < m_layout.hwOutputs; i++) {
        if (m_loopback[i] && loopbackBufs[i]) {
            memcpy(loopbackBufs[i], m_mixBufs[i].data(), bufferSize * sizeof(float));
        }
    }
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
    for (int i = 0; i < m_layout.hwOutputs; i++) {
        AsioChannelDesc desc;
        desc.channelIndex = ch;
        desc.hwChannelIndex = i;
        desc.isVirtual = false;
        desc.isInput = false;
        char name[32];
        
        if (m_layout.hwOutputs == 20) {
            if (i < 10) snprintf(name, sizeof(name), "OUT %02d", i + 1);
            else if (i < 12) snprintf(name, sizeof(name), (i == 10) ? "SPDIF OUT L" : "SPDIF OUT R");
            else snprintf(name, sizeof(name), "ADAT OUT %d", i - 11);
        } else if (m_layout.hwOutputs == 14) {
            // e.g. UMC1820 without SPDIF or something, just in case
            if (i < 10) snprintf(name, sizeof(name), "OUT %02d", i + 1);
            else snprintf(name, sizeof(name), "ADAT OUT %d", i - 9);
        } else {
            snprintf(name, sizeof(name), "OUT %02d", i + 1);
        }
        
        desc.name = name;
        m_outputDescs.push_back(desc);
        ch++;
    }

    // === INPUT DESCRIPTORS ===
    ch = 0;
    for (int i = 0; i < m_layout.hwInputs; i++) {
        AsioChannelDesc desc;
        desc.channelIndex = ch;
        desc.hwChannelIndex = i;
        desc.isVirtual = false;
        desc.isInput = true;
        char name[32];

        if (m_layout.hwInputs == 18) {
            if (i < 8) snprintf(name, sizeof(name), "IN %02d", i + 1);
            else if (i < 10) snprintf(name, sizeof(name), (i == 8) ? "SPDIF IN L" : "SPDIF IN R");
            else snprintf(name, sizeof(name), "ADAT IN %d", i - 9);
        } else if (m_layout.hwInputs == 10) {
            if (i < 8) snprintf(name, sizeof(name), "IN %02d", i + 1);
            else snprintf(name, sizeof(name), "SPDIF IN %s", (i==8) ? "L" : "R");
        } else {
            snprintf(name, sizeof(name), "IN %02d", i + 1);
        }

        desc.name = name;
        m_inputDescs.push_back(desc);
        ch++;
    }

    LOG_INFO(LOG_MODULE, "Built %d output + %d input channel descriptors",
             (int)m_outputDescs.size(), (int)m_inputDescs.size());
}
