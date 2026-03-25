/*
 * UsbAudioProtocol.h - USB Audio Class 2.0 协议常量和结构定义
 *
 * 参考: USB Audio Class 2.0 Specification (Release 2.0, May 31, 2006)
 *       USB Audio 3.0 Device Class Definition for Audio Devices (2017)
 */

#pragma once
#include <cstdint>

// ============================================================================
// USB Audio Class Codes
// ============================================================================

// Audio Interface Class / Subclass / Protocol
#define USB_CLASS_AUDIO                 0x01

// Audio Interface Subclass Codes (A.2)
#define USB_AUDIO_SUBCLASS_UNDEFINED    0x00
#define USB_AUDIO_SUBCLASS_CONTROL      0x01  // AudioControl
#define USB_AUDIO_SUBCLASS_STREAMING    0x02  // AudioStreaming
#define USB_AUDIO_SUBCLASS_MIDISTREAM   0x03  // MIDIStreaming

// Audio Interface Protocol Codes (A.3)
#define USB_AUDIO_PROTOCOL_UNDEFINED    0x00
#define USB_AUDIO_PROTOCOL_IP_VER_02    0x20  // IP version 2.0 (UAC2)

// ============================================================================
// Audio Class-Specific Descriptor Types (A.4)
// ============================================================================
#define USB_CS_UNDEFINED                0x20
#define USB_CS_DEVICE                   0x21
#define USB_CS_CONFIGURATION            0x22
#define USB_CS_STRING                   0x23
#define USB_CS_INTERFACE                0x24
#define USB_CS_ENDPOINT                 0x25

// ============================================================================
// AudioControl Interface Descriptor Subtypes (A.5)
// ============================================================================
#define UAC2_AC_HEADER                  0x01
#define UAC2_AC_INPUT_TERMINAL          0x02
#define UAC2_AC_OUTPUT_TERMINAL         0x03
#define UAC2_AC_MIXER_UNIT              0x04
#define UAC2_AC_SELECTOR_UNIT           0x05
#define UAC2_AC_FEATURE_UNIT            0x06
#define UAC2_AC_EFFECT_UNIT             0x07
#define UAC2_AC_PROCESSING_UNIT         0x08
#define UAC2_AC_EXTENSION_UNIT          0x09
#define UAC2_AC_CLOCK_SOURCE            0x0A
#define UAC2_AC_CLOCK_SELECTOR          0x0B
#define UAC2_AC_CLOCK_MULTIPLIER        0x0C
#define UAC2_AC_SAMPLE_RATE_CONVERTER   0x0D

// ============================================================================
// AudioStreaming Interface Descriptor Subtypes (A.6)
// ============================================================================
#define UAC2_AS_GENERAL                 0x01
#define UAC2_AS_FORMAT_TYPE             0x02
#define UAC2_AS_ENCODER                 0x03
#define UAC2_AS_DECODER                 0x04

// ============================================================================
// Audio Endpoint Descriptor Subtypes
// ============================================================================
#define UAC2_EP_GENERAL                 0x01

// ============================================================================
// Terminal Types (Audio Terminal Types - Table 2-1/2-2)
// ============================================================================
#define UAC2_TT_USB_UNDEFINED           0x0100
#define UAC2_TT_USB_STREAMING           0x0101  // USB Streaming
#define UAC2_TT_USB_VENDOR_SPECIFIC     0x01FF

#define UAC2_TT_INPUT_UNDEFINED         0x0200
#define UAC2_TT_MICROPHONE              0x0201
#define UAC2_TT_DESKTOP_MIC             0x0202
#define UAC2_TT_PERSONAL_MIC            0x0203
#define UAC2_TT_OMNI_MIC                0x0204
#define UAC2_TT_MIC_ARRAY               0x0205
#define UAC2_TT_PROC_MIC_ARRAY          0x0206

#define UAC2_TT_OUTPUT_UNDEFINED        0x0300
#define UAC2_TT_SPEAKER                 0x0301
#define UAC2_TT_HEADPHONES              0x0302
#define UAC2_TT_HEAD_MOUNTED            0x0303
#define UAC2_TT_DESKTOP_SPEAKER         0x0304

#define UAC2_TT_EXTERNAL_UNDEFINED      0x0600
#define UAC2_TT_ANALOG_CONNECTOR        0x0601
#define UAC2_TT_DIGITAL_AUDIO           0x0602
#define UAC2_TT_LINE_CONNECTOR          0x0603
#define UAC2_TT_SPDIF                   0x0605
#define UAC2_TT_ADAT                    0x0607

// ============================================================================
// Audio Data Format Type I Codes (A.7)
// ============================================================================
#define UAC2_FORMAT_TYPE_I              0x01

// Audio Data Format Codes
#define UAC2_FORMAT_PCM                 0x00000001
#define UAC2_FORMAT_PCM8                0x00000002
#define UAC2_FORMAT_IEEE_FLOAT          0x00000004
#define UAC2_FORMAT_ALAW                0x00000008
#define UAC2_FORMAT_MULAW               0x00000010
#define UAC2_FORMAT_RAW_DATA            0x80000000

// ============================================================================
// Audio Class-Specific Request Codes (A.14)
// ============================================================================
#define UAC2_CUR                        0x01  // Current setting
#define UAC2_RANGE                      0x02  // Range (min, max, res)

// Control Selector Codes
#define UAC2_CS_SAM_FREQ_CONTROL        0x01  // Clock Source: Sampling Frequency

// Feature Unit Control Selectors
#define UAC2_FU_MUTE_CONTROL            0x01
#define UAC2_FU_VOLUME_CONTROL          0x02

// ============================================================================
// Packed Structures for USB Audio Descriptors
// ============================================================================
#pragma pack(push, 1)

// Clock Source Descriptor (4.7.2.1)
struct uac2_clock_source_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;       // CS_INTERFACE
    uint8_t  bDescriptorSubtype;    // CLOCK_SOURCE
    uint8_t  bClockID;
    uint8_t  bmAttributes;          // 0=External, 1=Internal Fixed, 2=Internal Variable, 3=Internal Programmable
    uint8_t  bmControls;
    uint8_t  bAssocTerminal;
    uint8_t  iClockSource;
};

// Clock Selector Descriptor (4.7.2.2)
struct uac2_clock_selector_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;    // CLOCK_SELECTOR
    uint8_t  bClockID;
    uint8_t  bNrInPins;
    // followed by baCSourceID[bNrInPins], bmControls, iClockSelector
};

// Input Terminal Descriptor (4.7.2.4)
struct uac2_input_terminal_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;    // INPUT_TERMINAL
    uint8_t  bTerminalID;
    uint16_t wTerminalType;
    uint8_t  bAssocTerminal;
    uint8_t  bCSourceID;            // Clock Source ID
    uint8_t  bNrChannels;
    uint32_t bmChannelConfig;
    uint8_t  iChannelNames;
    uint16_t bmControls;
    uint8_t  iTerminal;
};

// Output Terminal Descriptor (4.7.2.5)
struct uac2_output_terminal_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;    // OUTPUT_TERMINAL
    uint8_t  bTerminalID;
    uint16_t wTerminalType;
    uint8_t  bAssocTerminal;
    uint8_t  bSourceID;             // Connected to this unit
    uint8_t  bCSourceID;            // Clock Source ID
    uint16_t bmControls;
    uint8_t  iTerminal;
};

// Feature Unit Descriptor (4.7.2.8) - variable length
struct uac2_feature_unit_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;    // FEATURE_UNIT
    uint8_t  bUnitID;
    uint8_t  bSourceID;
    // followed by bmaControls[] (4 bytes each), iFeature
};

// AudioStreaming Interface Descriptor (4.9.2)
struct uac2_as_general_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;    // AS_GENERAL
    uint8_t  bTerminalLink;
    uint8_t  bmControls;
    uint8_t  bFormatType;           // FORMAT_TYPE_I
    uint32_t bmFormats;             // PCM, IEEE_FLOAT, etc.
    uint8_t  bNrChannels;
    uint32_t bmChannelConfig;
    uint8_t  iChannelNames;
};

// Format Type I Descriptor (UAC2 - 2.3.1.6)
struct uac2_format_type_i_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;    // FORMAT_TYPE
    uint8_t  bFormatType;           // FORMAT_TYPE_I
    uint8_t  bSubslotSize;          // Bytes per subslot (sample container)
    uint8_t  bBitResolution;        // Actual bits of audio data
};

// Class-Specific AS Isochronous Endpoint Descriptor
struct uac2_iso_endpoint_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;       // CS_ENDPOINT
    uint8_t  bDescriptorSubtype;    // EP_GENERAL
    uint8_t  bmAttributes;          // bit 7 = MaxPacketsOnly
    uint8_t  bmControls;
    uint8_t  bLockDelayUnits;
    uint16_t wLockDelay;
};

#pragma pack(pop)

// ============================================================================
// Helper: Terminal type name
// ============================================================================
inline const char* uac2_terminal_type_name(uint16_t type) {
    switch (type) {
        case UAC2_TT_USB_STREAMING:     return "USB Streaming";
        case UAC2_TT_MICROPHONE:        return "Microphone";
        case UAC2_TT_SPEAKER:           return "Speaker";
        case UAC2_TT_HEADPHONES:        return "Headphones";
        case UAC2_TT_LINE_CONNECTOR:    return "Line Connector";
        case UAC2_TT_SPDIF:             return "S/PDIF";
        case UAC2_TT_ADAT:              return "ADAT";
        case UAC2_TT_ANALOG_CONNECTOR:  return "Analog";
        case UAC2_TT_DIGITAL_AUDIO:     return "Digital Audio";
        default: return "Unknown";
    }
}
