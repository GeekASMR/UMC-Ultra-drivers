/*
 * ASIO Interface Definition
 * Based on Steinberg ASIO 2.3 SDK
 * 
 * This defines all ASIO types, structures, and function prototypes
 * required for implementing an ASIO driver.
 */

#ifndef __ASIO_H__
#define __ASIO_H__

#include "asiosys.h"
#include <windows.h>

//-------------------------------------------------------------------
// ASIO Error Codes
//-------------------------------------------------------------------

typedef long ASIOError;

enum {
    ASE_OK = 0,              // This value will be returned whenever the call succeeded
    ASE_SUCCESS = 0x3f4847a0,// unique success return value for ASIOFuture calls
    ASE_NotPresent = -1000,  // hardware input or output is not present or available
    ASE_HWMalfunction,      // hardware is malfunctioning
    ASE_InvalidParameter,    // input parameter invalid
    ASE_InvalidMode,         // hardware is in a bad mode or used in a bad mode
    ASE_SPNotAdvancing,      // hardware is not running
    ASE_NoClock,             // sample clock or rate cannot be determined or is not present
    ASE_NoMemory             // not enough memory for completing the request
};

//-------------------------------------------------------------------
// ASIO Bool Type
//-------------------------------------------------------------------

typedef long ASIOBool;
enum {
    ASIOFalse = 0,
    ASIOTrue = 1
};

//-------------------------------------------------------------------
// ASIO Sample Rate
//-------------------------------------------------------------------

typedef double ASIOSampleRate;

//-------------------------------------------------------------------
// ASIO Sample Types
//-------------------------------------------------------------------

typedef long ASIOSampleType;
enum {
    ASIOSTInt16MSB   = 0,
    ASIOSTInt24MSB   = 1,    // used for 20 bits as well
    ASIOSTInt32MSB   = 2,
    ASIOSTFloat32MSB = 3,    // IEEE 754 32 bit float
    ASIOSTFloat64MSB = 4,    // IEEE 754 64 bit double float

    // these are used for 32 bit data buffer, with different alignment of the data inside
    // 32 bit PCI bus systems can be more easily used with these
    ASIOSTInt32MSB16 = 8,    // 32 bit data with 16 bit alignment
    ASIOSTInt32MSB18 = 9,    // 32 bit data with 18 bit alignment
    ASIOSTInt32MSB20 = 10,   // 32 bit data with 20 bit alignment
    ASIOSTInt32MSB24 = 11,   // 32 bit data with 24 bit alignment
    
    ASIOSTInt16LSB   = 16,
    ASIOSTInt24LSB   = 17,
    ASIOSTInt32LSB   = 18,
    ASIOSTFloat32LSB = 19,   // IEEE 754 32 bit float, as found on Intel x86 architecture
    ASIOSTFloat64LSB = 20,   // IEEE 754 64 bit double float, as found on Intel x86 architecture

    // these are used for 32 bit data buffer, with different alignment of the data inside
    // 32 bit PCI bus systems can be more easily used with these
    ASIOSTInt32LSB16 = 24,   // 32 bit data with 16 bit alignment
    ASIOSTInt32LSB18 = 25,   // 32 bit data with 18 bit alignment
    ASIOSTInt32LSB20 = 26,   // 32 bit data with 20 bit alignment
    ASIOSTInt32LSB24 = 27,   // 32 bit data with 24 bit alignment

    //  ASIO DSD format.
    ASIOSTDSDInt8LSB1 = 32,  // DSD 1 bit data, 8 samples per byte. First sample in Least significant bit.
    ASIOSTDSDInt8MSB1 = 33,  // DSD 1 bit data, 8 samples per byte. First sample in Most significant bit.
    ASIOSTDSDInt8NER8 = 40,  // DSD 8 bit data, 1 sample per byte. No Endianness required.

    ASIOSTLastEntry
};

//-------------------------------------------------------------------
// ASIO Clock Sources
//-------------------------------------------------------------------

typedef struct ASIOClockSource {
    long index;              // as used for ASIOSetClockSource()
    long associatedChannel;  // the first channel of a stereo pair
    long associatedGroup;    // say, for digital/analog
    ASIOBool isCurrentSource;// indicated by ASIOTrue
    char name[32];           // for user selection
} ASIOClockSource;

//-------------------------------------------------------------------
// ASIO Channel Info
//-------------------------------------------------------------------

typedef struct ASIOChannelInfo {
    long channel;            // on input, channel index
    ASIOBool isInput;        // on input
    ASIOBool isActive;       // on exit
    long channelGroup;       // dto
    ASIOSampleType type;     // dto
    char name[32];           // dto
} ASIOChannelInfo;

//-------------------------------------------------------------------
// ASIO Buffer Info
//-------------------------------------------------------------------

typedef struct ASIOBufferInfo {
    ASIOBool isInput;        // on input: ASIOTrue: input, else output
    long channelNum;         // on input: channel index
    void *buffers[2];        // on output: double buffer addresses
} ASIOBufferInfo;

//-------------------------------------------------------------------
// ASIO Time Structures
//-------------------------------------------------------------------

typedef struct ASIOTimeCode {
    double speed;            // speed relation (fraction of nominal speed)
    long long timeCodeSamples; // time in samples
    unsigned long flags;     // some information flags (cycled by cycleCount)
    char future[64];
} ASIOTimeCode;

enum ASIOTimeCodeFlags {
    kTcValid       = 1,
    kTcRunning     = 1 << 1,
    kTcReverse     = 1 << 2,
    kTcOnspeed     = 1 << 3,
    kTcStill       = 1 << 4,
    kTcSpeedValid  = 1 << 8
};

typedef struct AsioTimeInfo {
    double speed;            // absolute speed (1. = nominal)
    long long systemTime;    // system time related to samplePosition, in nanoseconds
    long long samplePosition;// sample position since ASIOStart()
    ASIOSampleRate sampleRate;// current rate
    unsigned long flags;     // (cycled by cycleCount)
    char reserved[12];
} AsioTimeInfo;

enum AsioTimeInfoFlags {
    kSystemTimeValid     = 1,
    kSamplePositionValid = 1 << 1,
    kSampleRateValid     = 1 << 2,
    kSpeedValid          = 1 << 3,
    kSampleRateChanged   = 1 << 4,
    kClockSourceChanged  = 1 << 5
};

typedef struct ASIOTime {
    long reserved[4];
    AsioTimeInfo timeInfo;
    ASIOTimeCode timeCode;
} ASIOTime;

//-------------------------------------------------------------------
// ASIO Callbacks
//-------------------------------------------------------------------

typedef void (*ASIOBufferSwitchFunc)(long doubleBufferIndex, ASIOBool directProcess);
typedef void (*ASIOSampleRateDidChangeFunc)(ASIOSampleRate sRate);
typedef long (*ASIOAsioMessageFunc)(long selector, long value, void* message, double* opt);
typedef ASIOTime* (*ASIOBufferSwitchTimeInfoFunc)(ASIOTime *params, long doubleBufferIndex, ASIOBool directProcess);

typedef struct ASIOCallbacks {
    ASIOBufferSwitchFunc bufferSwitch;
    ASIOSampleRateDidChangeFunc sampleRateDidChange;
    ASIOAsioMessageFunc asioMessage;
    ASIOBufferSwitchTimeInfoFunc bufferSwitchTimeInfo;
} ASIOCallbacks;

//-------------------------------------------------------------------
// ASIO Message Selectors
//-------------------------------------------------------------------

enum {
    kAsioSelectorSupported = 1,
    kAsioEngineVersion,
    kAsioResetRequest,
    kAsioBufferSizeChange,
    kAsioResyncRequest,
    kAsioLatenciesChanged,
    kAsioSupportsTimeInfo,
    kAsioSupportsTimeCode,
    kAsioMMCCommand,
    kAsioSupportsInputMonitor,
    kAsioSupportsInputGain,
    kAsioSupportsInputMeter,
    kAsioSupportsOutputGain,
    kAsioSupportsOutputMeter,
    kAsioOverload,
    kAsioNumMessageSelectors
};

//-------------------------------------------------------------------
// ASIO Future Selectors
//-------------------------------------------------------------------

enum {
    kAsioEnableTimeCodeRead = 1,
    kAsioDisableTimeCodeRead,
    kAsioSetInputMonitor,
    kAsioTransport,
    kAsioSetInputGain,
    kAsioGetInputMeter,
    kAsioSetOutputGain,
    kAsioGetOutputMeter,
    kAsioCanInputMonitor,
    kAsioCanTimeInfo,
    kAsioCanTimeCode,
    kAsioCanTransport,
    kAsioCanInputGain,
    kAsioCanInputMeter,
    kAsioCanOutputGain,
    kAsioCanOutputMeter,
    kAsioOptionalOne,

    //  DSD support
    //  The following extensions are required to allow switching
    //  and control of the DSD subsystem.
    kAsioSetIoFormat = 0x23111961,      // ASIOIoFormat * in params.
    kAsioGetIoFormat = 0x23111983,      // ASIOIoFormat * in params.
    kAsioCanDoIoFormat = 0x23112004,    // ASIOIoFormat * in params.

    kAsioCanReportOverload = 0x24042012,
    kAsioGetInternalBufferSamples = 0x25042012
};

//-------------------------------------------------------------------
// ASIO Input Monitor
//-------------------------------------------------------------------

typedef struct ASIOInputMonitor {
    long input;     // this input was set to monitor (or off), -1: all
    long output;    // suggested output for monitoring the input (maybe ignored)
    long gain;      // suggested gain, ranging 0 - 0x7fffffffL (-inf to +12 dB)
    ASIOBool state; // ASIOTrue => on, ASIOFalse => off
    long pan;       // suggested pan, 0 => all left, 0x7fffffff => right
} ASIOInputMonitor;

#endif // __ASIO_H__
