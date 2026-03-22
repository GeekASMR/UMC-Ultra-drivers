/*
 * SharedMemoryBridge - 用户态共享内存 IPC
 * 
 * 基于 ASMRTOP Driver V2 (公版 VirtualAudioRouter) 的 IPC 机制改造,
 * 从内核态 (ZwCreateSection) 改为用户态 (CreateFileMapping).
 * 
 * 用于虚拟通道的音频数据传输:
 *   - PLAYBACK 通道: DAW -> 共享内存 -> 路由矩阵 -> 物理输出
 *   - VIRTUAL REC 通道: 物理输入 -> 路由矩阵 -> 共享内存 -> DAW
 *   - Loopback 通道: 物理输出混音 -> 共享内存 -> DAW 录制
 */

#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <atomic>

// Ring buffer size: 131072 samples (~3 seconds @44.1kHz)
// Matches ASMRTOP Driver V2's proven buffer size
#define IPC_RING_SIZE       131072
#define IPC_RING_MASK       (IPC_RING_SIZE - 1)

// IPC shared memory layout (matches ASMRTOP's IpcAudioBuffer)
struct IpcAudioBuffer {
    volatile ULONG writePos;        // Write cursor (producer increments)
    volatile ULONG readPos;         // Read cursor (consumer increments)
    volatile ULONG sampleRate;      // Current sample rate
    volatile ULONG channelCount;    // Channels in this buffer (1=mono, 2=stereo)
    volatile ULONG flags;           // Status flags
    ULONG reserved[3];              // Alignment padding to 32 bytes header
    float ring[IPC_RING_SIZE * 2];  // Interleaved L/R ring buffer
};

// IPC status flags
#define IPC_FLAG_ACTIVE     0x01    // Buffer is actively streaming
#define IPC_FLAG_OVERFLOW   0x02    // Write overflow detected
#define IPC_FLAG_UNDERFLOW  0x04    // Read underflow detected

// IPC naming: \\BaseNamedObjects\\UMCStudio_{direction}_{channelId}
// Uses public version naming convention (not branded)
#define IPC_NAME_PREFIX     L"UMCStudio"

// Direction identifiers
enum class IpcDirection {
    Playback,       // DAW -> virtual output -> routing -> hw output
    Capture,        // hw input -> routing -> virtual input -> DAW
    Loopback        // hw output mix -> loopback -> DAW
};

class SharedMemoryChannel {
public:
    SharedMemoryChannel();
    ~SharedMemoryChannel();

    // Create or open a shared memory channel
    bool create(IpcDirection direction, int channelId, int sampleRate = 44100);
    void close();

    bool isOpen() const { return m_pBuffer != nullptr; }

    // --- Producer API (write audio into ring buffer) ---
    
    // Write interleaved stereo float samples
    void writeStereo(const float* left, const float* right, int numFrames);
    
    // Write mono float samples (duplicated to both channels)
    void writeMono(const float* data, int numFrames);

    // --- Consumer API (read audio from ring buffer) ---
    
    // Read interleaved stereo float samples
    // Returns actual frames read (may be less than requested if underflow)
    int readStereo(float* left, float* right, int numFrames);

    // --- Status ---
    int getAvailableFrames() const;
    void setActive(bool active);
    bool isActive() const;
    void reset();

    const std::wstring& getName() const { return m_name; }

private:
    HANDLE m_hMapping;
    IpcAudioBuffer* m_pBuffer;
    std::wstring m_name;

    static std::wstring buildName(IpcDirection direction, int channelId);
};

// Manages all IPC channels for virtual routing
class SharedMemoryBridge {
public:
    SharedMemoryBridge();
    ~SharedMemoryBridge();

    // Initialize all virtual channels based on routing layout
    bool init(int numPlaybackPairs, int numLoopbackChannels, 
              int numVirtualRecPairs, int sampleRate);
    void shutdown();

    // Access individual channels
    SharedMemoryChannel* getPlaybackChannel(int index);
    SharedMemoryChannel* getLoopbackChannel(int index);
    SharedMemoryChannel* getVirtualRecChannel(int index);

    int getPlaybackCount()   const { return (int)m_playbackChannels.size(); }
    int getLoopbackCount()   const { return (int)m_loopbackChannels.size(); }
    int getVirtualRecCount() const { return (int)m_virtualRecChannels.size(); }

    // Reset all channels (on stream start)
    void resetAll();
    void setAllActive(bool active);

private:
    std::vector<SharedMemoryChannel> m_playbackChannels;
    std::vector<SharedMemoryChannel> m_loopbackChannels;
    std::vector<SharedMemoryChannel> m_virtualRecChannels;
};
