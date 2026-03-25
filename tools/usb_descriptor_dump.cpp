/*
 * usb_descriptor_dump.cpp - USB 描述符 Dump 工具
 *
 * 用途: 通过 libusb 读取百灵达 UMC 1820 的完整 USB 描述符，
 *       解析 USB Audio Class 2.0 的所有接口、端点和音频参数。
 *
 * 编译: cl /EHsc /I../src /I../libusb/include usb_descriptor_dump.cpp 
 *       /link ../libusb/VS2022/MS64/dll/libusb-1.0.lib
 *
 * 运行前提: 用 Zadig 将 UMC 1820 驱动替换为 WinUSB
 *           或者: 以管理员权限运行（可列出部分信息）
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include "libusb.h"
#include "usb/UsbAudioProtocol.h"

#define UMC_VID 0x1397
#define UMC_PID 0x0503

// ============================================================================
// Helper: hex dump
// ============================================================================
void hexdump(const uint8_t* data, int len, int indent = 4) {
    for (int i = 0; i < len; i++) {
        if (i % 16 == 0 && i > 0) printf("\n%*s", indent, "");
        if (i == 0) printf("%*s", indent, "");
        printf("%02X ", data[i]);
    }
    printf("\n");
}

// ============================================================================
// Parse UAC2 Audio Control descriptors
// ============================================================================
void parse_ac_descriptor(const uint8_t* data, int len) {
    if (len < 3) return;
    uint8_t subtype = data[2];

    switch (subtype) {
    case UAC2_AC_HEADER: {
        printf("    [AC HEADER] UAC Version: %d.%02d, Total Length: %d, Category: 0x%02X\n",
               data[4], data[3], data[5] | (data[6] << 8), len > 7 ? data[7] : 0);
        break;
    }
    case UAC2_AC_CLOCK_SOURCE: {
        if (len >= sizeof(uac2_clock_source_desc)) {
            auto* cs = (const uac2_clock_source_desc*)data;
            const char* clockType[] = {"External", "Internal Fixed", "Internal Variable", "Internal Programmable"};
            printf("    [CLOCK SOURCE] ID=%d, Type=%s, Sync=%s\n",
                   cs->bClockID,
                   clockType[cs->bmAttributes & 0x03],
                   (cs->bmAttributes & 0x04) ? "SyncToSOF" : "Free-running");
        }
        break;
    }
    case UAC2_AC_CLOCK_SELECTOR: {
        auto* cs = (const uac2_clock_selector_desc*)data;
        printf("    [CLOCK SELECTOR] ID=%d, Pins=%d, Sources: ", cs->bClockID, cs->bNrInPins);
        for (int i = 0; i < cs->bNrInPins && (5 + i) < len; i++) {
            printf("%d ", data[5 + i]);
        }
        printf("\n");
        break;
    }
    case UAC2_AC_INPUT_TERMINAL: {
        if (len >= 17) {
            auto* it = (const uac2_input_terminal_desc*)data;
            printf("    [INPUT TERMINAL] ID=%d, Type=0x%04X (%s), Channels=%d, ClockSource=%d\n",
                   it->bTerminalID, it->wTerminalType,
                   uac2_terminal_type_name(it->wTerminalType),
                   it->bNrChannels, it->bCSourceID);
        }
        break;
    }
    case UAC2_AC_OUTPUT_TERMINAL: {
        if (len >= 12) {
            auto* ot = (const uac2_output_terminal_desc*)data;
            printf("    [OUTPUT TERMINAL] ID=%d, Type=0x%04X (%s), Source=%d, ClockSource=%d\n",
                   ot->bTerminalID, ot->wTerminalType,
                   uac2_terminal_type_name(ot->wTerminalType),
                   ot->bSourceID, ot->bCSourceID);
        }
        break;
    }
    case UAC2_AC_FEATURE_UNIT: {
        if (len >= 6) {
            auto* fu = (const uac2_feature_unit_desc*)data;
            int numControls = (len - 6) / 4;
            printf("    [FEATURE UNIT] ID=%d, Source=%d, Controls=%d\n",
                   fu->bUnitID, fu->bSourceID, numControls);
        }
        break;
    }
    case UAC2_AC_MIXER_UNIT:
        printf("    [MIXER UNIT] ID=%d\n", data[3]);
        break;
    case UAC2_AC_SELECTOR_UNIT:
        printf("    [SELECTOR UNIT] ID=%d\n", data[3]);
        break;
    case UAC2_AC_EXTENSION_UNIT:
        printf("    [EXTENSION UNIT] ID=%d\n", data[3]);
        break;
    default:
        printf("    [AC UNKNOWN subtype=0x%02X]\n", subtype);
        break;
    }
}

// ============================================================================
// Parse UAC2 Audio Streaming descriptors
// ============================================================================
void parse_as_descriptor(const uint8_t* data, int len) {
    if (len < 3) return;
    uint8_t subtype = data[2];

    switch (subtype) {
    case UAC2_AS_GENERAL: {
        if (len >= 16) {
            auto* as = (const uac2_as_general_desc*)data;
            printf("    [AS GENERAL] TerminalLink=%d, Format=0x%08X, Channels=%d\n",
                   as->bTerminalLink, as->bmFormats, as->bNrChannels);
            printf("      Formats:");
            if (as->bmFormats & UAC2_FORMAT_PCM) printf(" PCM");
            if (as->bmFormats & UAC2_FORMAT_IEEE_FLOAT) printf(" IEEE_FLOAT");
            if (as->bmFormats & UAC2_FORMAT_RAW_DATA) printf(" RAW");
            printf("\n");
        }
        break;
    }
    case UAC2_AS_FORMAT_TYPE: {
        if (len >= 6) {
            auto* ft = (const uac2_format_type_i_desc*)data;
            printf("    [FORMAT TYPE I] SubslotSize=%d bytes, BitResolution=%d bits\n",
                   ft->bSubslotSize, ft->bBitResolution);
        }
        break;
    }
    default:
        printf("    [AS subtype=0x%02X]\n", subtype);
        break;
    }
}

// ============================================================================
// Query supported sample rates via UAC2 RANGE request
// ============================================================================
void query_sample_rates(libusb_device_handle* handle, uint8_t clockSourceId, uint8_t interfaceNum) {
    uint8_t buf[256] = {};
    // UAC2 RANGE request: GET bRequest=0x02, wValue=CS_SAM_FREQ_CONTROL<<8, wIndex=clockSourceId<<8
    int ret = libusb_control_transfer(handle,
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
        UAC2_RANGE,
        (UAC2_CS_SAM_FREQ_CONTROL << 8),
        (clockSourceId << 8) | interfaceNum,
        buf, sizeof(buf), 1000);

    if (ret >= 2) {
        uint16_t numRanges = buf[0] | (buf[1] << 8);
        printf("    [SAMPLE RATE RANGE] %d ranges:\n", numRanges);
        for (int i = 0; i < numRanges && (2 + i * 12 + 11) < ret; i++) {
            int off = 2 + i * 12;
            uint32_t min = buf[off] | (buf[off+1] << 8) | (buf[off+2] << 16) | (buf[off+3] << 24);
            uint32_t max = buf[off+4] | (buf[off+5] << 8) | (buf[off+6] << 16) | (buf[off+7] << 24);
            uint32_t res = buf[off+8] | (buf[off+9] << 8) | (buf[off+10] << 16) | (buf[off+11] << 24);
            if (min == max) {
                printf("      %d Hz\n", min);
            } else {
                printf("      %d - %d Hz (step %d)\n", min, max, res);
            }
        }
    } else {
        printf("    [SAMPLE RATE RANGE] Query failed (ret=%d, need WinUSB driver?)\n", ret);
    }
}

// ============================================================================
// Main
// ============================================================================
int main() {
    printf("===============================================\n");
    printf("  USB Audio Descriptor Dump for UMC 1820\n");
    printf("  VID=0x%04X PID=0x%04X\n", UMC_VID, UMC_PID);
    printf("===============================================\n\n");

    int rc = libusb_init(NULL);
    if (rc < 0) {
        printf("ERROR: libusb_init failed: %s\n", libusb_error_name(rc));
        return 1;
    }

    // Find device
    libusb_device** devs;
    ssize_t cnt = libusb_get_device_list(NULL, &devs);
    if (cnt < 0) {
        printf("ERROR: libusb_get_device_list failed\n");
        libusb_exit(NULL);
        return 1;
    }

    libusb_device* target = NULL;
    for (ssize_t i = 0; i < cnt; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devs[i], &desc) == 0) {
            if (desc.idVendor == UMC_VID && desc.idProduct == UMC_PID) {
                target = devs[i];
                printf("Found UMC 1820 at bus %d, addr %d\n",
                       libusb_get_bus_number(target), libusb_get_device_address(target));
                printf("  USB Version: %d.%02d\n", desc.bcdUSB >> 8, desc.bcdUSB & 0xFF);
                printf("  Device Class: 0x%02X (SubClass=0x%02X, Protocol=0x%02X)\n",
                       desc.bDeviceClass, desc.bDeviceSubClass, desc.bDeviceProtocol);
                printf("  Max Packet Size EP0: %d\n", desc.bMaxPacketSize0);
                printf("  Configurations: %d\n\n", desc.bNumConfigurations);
                break;
            }
        }
    }

    if (!target) {
        printf("ERROR: UMC 1820 not found! Is it connected?\n");
        libusb_free_device_list(devs, 1);
        libusb_exit(NULL);
        return 1;
    }

    // Get full config descriptor
    struct libusb_config_descriptor* config;
    rc = libusb_get_active_config_descriptor(target, &config);
    if (rc < 0) {
        printf("ERROR: libusb_get_active_config_descriptor: %s\n", libusb_error_name(rc));
        // Try config 0
        rc = libusb_get_config_descriptor(target, 0, &config);
        if (rc < 0) {
            printf("ERROR: libusb_get_config_descriptor: %s\n", libusb_error_name(rc));
            libusb_free_device_list(devs, 1);
            libusb_exit(NULL);
            return 1;
        }
    }

    printf("=== Configuration Descriptor ===\n");
    printf("  Interfaces: %d\n", config->bNumInterfaces);
    printf("  Total Length: %d bytes\n", config->wTotalLength);
    printf("  Power: %d mA\n\n", config->MaxPower * 2);

    // Track clock source IDs for sample rate queries
    std::vector<uint8_t> clockSourceIds;
    uint8_t acInterfaceNum = 0xFF;

    // Iterate interfaces
    for (int i = 0; i < config->bNumInterfaces; i++) {
        const struct libusb_interface& iface = config->interface[i];
        printf("--- Interface %d (%d alt settings) ---\n", i, iface.num_altsetting);

        for (int a = 0; a < iface.num_altsetting; a++) {
            const struct libusb_interface_descriptor& alt = iface.altsetting[a];
            printf("  AltSetting %d: Class=0x%02X SubClass=0x%02X Protocol=0x%02X Endpoints=%d\n",
                   alt.bAlternateSetting, alt.bInterfaceClass,
                   alt.bInterfaceSubClass, alt.bInterfaceProtocol,
                   alt.bNumEndpoints);

            // Identify interface type
            const char* ifaceType = "Unknown";
            if (alt.bInterfaceClass == USB_CLASS_AUDIO) {
                if (alt.bInterfaceSubClass == USB_AUDIO_SUBCLASS_CONTROL) {
                    ifaceType = "AudioControl";
                    acInterfaceNum = alt.bInterfaceNumber;
                } else if (alt.bInterfaceSubClass == USB_AUDIO_SUBCLASS_STREAMING) {
                    ifaceType = "AudioStreaming";
                } else if (alt.bInterfaceSubClass == USB_AUDIO_SUBCLASS_MIDISTREAM) {
                    ifaceType = "MIDIStreaming";
                }
            } else if (alt.bInterfaceClass == 0xFE) {
                ifaceType = "DFU (Firmware Update)";
            }
            printf("  Type: %s\n", ifaceType);

            // Parse class-specific descriptors
            if (alt.extra_length > 0 && alt.extra) {
                printf("  Class-Specific Descriptors (%d bytes):\n", alt.extra_length);
                int offset = 0;
                while (offset < alt.extra_length) {
                    uint8_t dLen = alt.extra[offset];
                    uint8_t dType = alt.extra[offset + 1];
                    if (dLen == 0) break;

                    if (dType == USB_CS_INTERFACE) {
                        if (alt.bInterfaceSubClass == USB_AUDIO_SUBCLASS_CONTROL) {
                            parse_ac_descriptor(alt.extra + offset, dLen);
                            // Track clock sources
                            if (dLen >= 3 && alt.extra[offset + 2] == UAC2_AC_CLOCK_SOURCE) {
                                clockSourceIds.push_back(alt.extra[offset + 3]);
                            }
                        } else if (alt.bInterfaceSubClass == USB_AUDIO_SUBCLASS_STREAMING) {
                            parse_as_descriptor(alt.extra + offset, dLen);
                        }
                    } else if (dType == USB_CS_ENDPOINT) {
                        if (dLen >= 8) {
                            auto* ep = (const uac2_iso_endpoint_desc*)(alt.extra + offset);
                            printf("    [EP GENERAL] Attr=0x%02X, LockDelay=%d (units=%d)\n",
                                   ep->bmAttributes, ep->wLockDelay, ep->bLockDelayUnits);
                        }
                    }
                    offset += dLen;
                }
            }

            // Parse endpoints
            for (int e = 0; e < alt.bNumEndpoints; e++) {
                const struct libusb_endpoint_descriptor& ep = alt.endpoint[e];
                const char* dir = (ep.bEndpointAddress & 0x80) ? "IN" : "OUT";
                const char* type = "???";
                switch (ep.bmAttributes & 0x03) {
                    case LIBUSB_TRANSFER_TYPE_CONTROL: type = "Control"; break;
                    case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS: type = "Isochronous"; break;
                    case LIBUSB_TRANSFER_TYPE_BULK: type = "Bulk"; break;
                    case LIBUSB_TRANSFER_TYPE_INTERRUPT: type = "Interrupt"; break;
                }
                const char* syncType = "";
                if ((ep.bmAttributes & 0x03) == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS) {
                    switch ((ep.bmAttributes >> 2) & 0x03) {
                        case 0: syncType = " (NoSync)"; break;
                        case 1: syncType = " (Async)"; break;
                        case 2: syncType = " (Adaptive)"; break;
                        case 3: syncType = " (Sync)"; break;
                    }
                }
                const char* usageType = "";
                if ((ep.bmAttributes & 0x03) == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS) {
                    switch ((ep.bmAttributes >> 4) & 0x03) {
                        case 0: usageType = " [Data]"; break;
                        case 1: usageType = " [Feedback]"; break;
                        case 2: usageType = " [ImplicitFB]"; break;
                    }
                }
                printf("  EP 0x%02X: %s %s%s%s, MaxPacket=%d, Interval=%d\n",
                       ep.bEndpointAddress, dir, type, syncType, usageType,
                       ep.wMaxPacketSize, ep.bInterval);

                // Parse EP extra descriptors
                if (ep.extra_length > 0 && ep.extra) {
                    int off = 0;
                    while (off < ep.extra_length) {
                        uint8_t dLen = ep.extra[off];
                        uint8_t dType = ep.extra[off + 1];
                        if (dLen == 0) break;
                        if (dType == USB_CS_ENDPOINT && dLen >= 8) {
                            auto* epd = (const uac2_iso_endpoint_desc*)(ep.extra + off);
                            printf("    [CS EP] Attr=0x%02X, LockDelay=%d (units=%d)\n",
                                   epd->bmAttributes, epd->wLockDelay, epd->bLockDelayUnits);
                        }
                        off += dLen;
                    }
                }
            }
            printf("\n");
        }
    }

    // Try to open device for sample rate queries
    libusb_device_handle* handle = NULL;
    rc = libusb_open(target, &handle);
    if (rc == 0 && handle && !clockSourceIds.empty()) {
        printf("=== Sample Rate Queries ===\n");
        for (uint8_t csId : clockSourceIds) {
            printf("Clock Source ID %d:\n", csId);
            query_sample_rates(handle, csId, acInterfaceNum);
        }
        libusb_close(handle);
    } else if (rc != 0) {
        printf("\n[NOTE] Cannot open device for sample rate query.\n");
        printf("       Error: %s\n", libusb_error_name(rc));
        printf("       This is expected if Thesycon driver is still active.\n");
        printf("       Use Zadig to switch to WinUSB for full access.\n");
    }

    libusb_free_config_descriptor(config);
    libusb_free_device_list(devs, 1);
    libusb_exit(NULL);

    printf("\n=== Done ===\n");
    return 0;
}
