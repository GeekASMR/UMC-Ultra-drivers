/*
 * IASIO Interface Definition
 * Based on Steinberg ASIO SDK
 *
 * Defines the COM-based IASIO interface that all ASIO drivers must implement.
 */

#ifndef __IASIODRV_H__
#define __IASIODRV_H__

#include <unknwn.h>
#include "asio.h"

//-------------------------------------------------------------------
// IASIO COM Interface
//-------------------------------------------------------------------

// {00000000-0000-0000-C000-000000000046} - IUnknown
// The IASIO interface extends IUnknown

interface IASIO : public IUnknown
{
    // Initialization
    virtual ASIOBool init(void *sysHandle) = 0;
    
    // Driver Information
    virtual void getDriverName(char *name) = 0;
    virtual long getDriverVersion() = 0;
    virtual void getErrorMessage(char *string) = 0;
    
    // Audio Streaming
    virtual ASIOError start() = 0;
    virtual ASIOError stop() = 0;
    
    // Channel Information
    virtual ASIOError getChannels(long *numInputChannels, long *numOutputChannels) = 0;
    virtual ASIOError getLatencies(long *inputLatency, long *outputLatency) = 0;
    
    // Buffer Management
    virtual ASIOError getBufferSize(long *minSize, long *maxSize, long *preferredSize, long *granularity) = 0;
    virtual ASIOError canSampleRate(ASIOSampleRate sampleRate) = 0;
    virtual ASIOError getSampleRate(ASIOSampleRate *sampleRate) = 0;
    virtual ASIOError setSampleRate(ASIOSampleRate sampleRate) = 0;
    
    // Clock
    virtual ASIOError getClockSources(ASIOClockSource *clocks, long *numSources) = 0;
    virtual ASIOError setClockSource(long reference) = 0;
    virtual ASIOError getSamplePosition(long long *sPos, long long *tStamp) = 0;
    
    // Channel Details
    virtual ASIOError getChannelInfo(ASIOChannelInfo *info) = 0;
    
    // Buffer Operations
    virtual ASIOError createBuffers(ASIOBufferInfo *bufferInfos, long numChannels,
                                     long bufferSize, ASIOCallbacks *callbacks) = 0;
    virtual ASIOError disposeBuffers() = 0;
    
    // Control
    virtual ASIOError controlPanel() = 0;
    
    // Extension
    virtual ASIOError future(long selector, void *opt) = 0;
    virtual ASIOError outputReady() = 0;
};

#endif // __IASIODRV_H__
