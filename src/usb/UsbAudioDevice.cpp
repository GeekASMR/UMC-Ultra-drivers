/*
 * UsbAudioDevice.cpp - UAC2 Device Enumeration and Control
 */

#include "UsbAudioDevice.h"
#include "UsbAudioProtocol.h"
#include "../utils/Logger.h"
#include <iostream>

UsbAudioDevice::UsbAudioDevice() : m_ctx(nullptr), m_handle(nullptr), m_acInterfaceNum(0xFF) {
    int rc = libusb_init(&m_ctx);
    if (rc < 0) {
        LOG_ERROR("UsbAudioDevice", "libusb_init failed: %s", libusb_error_name(rc));
    }
}

UsbAudioDevice::~UsbAudioDevice() {
    close();
    if (m_ctx) {
        libusb_exit(m_ctx);
    }
}

bool UsbAudioDevice::open(uint16_t vid, uint16_t pid) {
    if (!m_ctx) {
        printf("[DEBUG] m_ctx is null\n");
        return false;
    }

    libusb_device** devs;
    ssize_t cnt = libusb_get_device_list(m_ctx, &devs);
    if (cnt < 0) {
        printf("[DEBUG] libusb_get_device_list failed\n");
        return false;
    }

    for (ssize_t i = 0; i < cnt; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devs[i], &desc) == 0) {
            if (desc.idVendor == vid && desc.idProduct == pid) {
                int r = libusb_open(devs[i], &m_handle);
                if (r == 0 && m_handle) {
                    printf("[DEBUG] Opened device at bus %d, addr %d\n", libusb_get_bus_number(devs[i]), libusb_get_device_address(devs[i]));
                    break;
                } else {
                    printf("[DEBUG] Failed to open matching device (r=%d)\n", r);
                }
            }
        }
    }
    libusb_free_device_list(devs, 1);

    if (!m_handle) {
        printf("[DEBUG] No matching valid device found for %04X:%04X\n", vid, pid);
        return false;
    }

    printf("[DEBUG] libusb_open SUCCESS\n");
    libusb_set_auto_detach_kernel_driver(m_handle, 1);

    if (!parseDescriptors()) {
        printf("[DEBUG] parseDescriptors failed!\n");
        close();
        return false;
    }

    printf("[DEBUG] parseDescriptors SUCCESS. Claiming interfaces...\n");
    // Claim all necessary interfaces
    int r;
    r = libusb_claim_interface(m_handle, m_acInterfaceNum);
    if (r < 0) printf("[DEBUG] Failed to claim AC interface %d: %s\n", m_acInterfaceNum, libusb_error_name(r));

    for (const auto& ep : m_captureEndpoints) {
        r = libusb_claim_interface(m_handle, ep.interfaceNum);
        if (r < 0) printf("[DEBUG] Failed to claim IN interface %d: %s\n", ep.interfaceNum, libusb_error_name(r));
    }
    for (const auto& ep : m_renderEndpoints) {
        r = libusb_claim_interface(m_handle, ep.interfaceNum);
        if (r < 0) printf("[DEBUG] Failed to claim OUT interface %d: %s\n", ep.interfaceNum, libusb_error_name(r));
    }

    printf("[DEBUG] Device open and configured successfully.\n");
    return true;
}

void UsbAudioDevice::close() {
    if (m_handle) {
        // Release interfaces
        libusb_release_interface(m_handle, m_acInterfaceNum);
        for (const auto& ep : m_captureEndpoints) {
            libusb_release_interface(m_handle, ep.interfaceNum);
        }
        for (const auto& ep : m_renderEndpoints) {
            libusb_release_interface(m_handle, ep.interfaceNum);
        }

        libusb_close(m_handle);
        m_handle = nullptr;
    }
    m_captureEndpoints.clear();
    m_renderEndpoints.clear();
    m_clockSourceIds.clear();
}

bool UsbAudioDevice::parseDescriptors() {
    libusb_device* dev = libusb_get_device(m_handle);
    struct libusb_config_descriptor* config;
    int rc = libusb_get_active_config_descriptor(dev, &config);
    if (rc < 0) {
        printf("[DEBUG] parseDescriptors: libusb_get_active_config_descriptor failed: %d\n", rc);
        rc = libusb_get_config_descriptor(dev, 0, &config);
        if (rc < 0) {
            printf("[DEBUG] parseDescriptors: libusb_get_config_descriptor fallback failed: %d\n", rc);
            return false;
        }
    }

    for (int i = 0; i < config->bNumInterfaces; i++) {
        const struct libusb_interface& iface = config->interface[i];
        
        // Find AudioControl interface
        if (iface.num_altsetting > 0) {
            const auto& alt0 = iface.altsetting[0];
            if (alt0.bInterfaceClass == USB_CLASS_AUDIO && alt0.bInterfaceSubClass == USB_AUDIO_SUBCLASS_CONTROL) {
                m_acInterfaceNum = alt0.bInterfaceNumber;
                
                // Parse for Clock Sources
                if (alt0.extra_length > 0) {
                    int offset = 0;
                    while (offset < alt0.extra_length) {
                        uint8_t dLen = alt0.extra[offset];
                        if (dLen == 0) break;
                        if (alt0.extra[offset+1] == USB_CS_INTERFACE && alt0.extra[offset+2] == UAC2_AC_CLOCK_SOURCE) {
                            m_clockSourceIds.push_back(alt0.extra[offset+3]);
                        }
                        offset += dLen;
                    }
                }
            }
        }

        // Find AudioStreaming interfaces
        for (int a = 0; a < iface.num_altsetting; a++) {
            const auto& alt = iface.altsetting[a];
            if (alt.bInterfaceClass == USB_CLASS_AUDIO && alt.bInterfaceSubClass == USB_AUDIO_SUBCLASS_STREAMING) {
                if (alt.bNumEndpoints > 0) {
                    AudioEndpointInfo info = {};
                    info.interfaceNum = alt.bInterfaceNumber;
                    info.altSetting = alt.bAlternateSetting;
                    
                    // Parse UAC2 AS General / Format
                    if (alt.extra_length > 0) {
                        int off = 0;
                        while(off < alt.extra_length) {
                            uint8_t dLen = alt.extra[off];
                            if(dLen==0) break;
                            if(alt.extra[off+1] == USB_CS_INTERFACE) {
                                if(alt.extra[off+2] == UAC2_AS_GENERAL && dLen >= 11) {
                                    info.numChannels = alt.extra[off+10];
                                } else if(alt.extra[off+2] == UAC2_AS_FORMAT_TYPE && dLen >= 6) {
                                    info.subslotSize = alt.extra[off+4];
                                    info.bitResolution = alt.extra[off+5];
                                }
                            }
                            off += dLen;
                        }
                    }

                    // Find Endpoints
                    for (int e = 0; e < alt.bNumEndpoints; e++) {
                        const auto& ep = alt.endpoint[e];
                        int trType = ep.bmAttributes & 0x03;
                        int usgType = (ep.bmAttributes >> 4) & 0x03;
                        int syncType = (ep.bmAttributes >> 2) & 0x03;

                        if (trType == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS) {
                            if (usgType == 0) { // Data endpoint
                                info.address = ep.bEndpointAddress;
                                info.direction = (ep.bEndpointAddress & 0x80) ? 1 : 0;
                                info.maxPacketSize = ep.wMaxPacketSize;
                                info.interval = ep.bInterval;
                                info.syncType = syncType;
                            } else if (usgType == 1) { // Feedback endpoint
                                info.feedbackEp = ep.bEndpointAddress;
                            }
                        }
                    }

                    if (info.direction == 1) {
                        m_captureEndpoints.push_back(info);
                    } else {
                        m_renderEndpoints.push_back(info);
                    }
                }
            }
        }
    }
    libusb_free_config_descriptor(config);
    return true;
}

bool UsbAudioDevice::setInterfaceAltSetting(uint8_t interfaceNum, uint8_t altSetting) {
    if (!m_handle) return false;
    int rc = libusb_set_interface_alt_setting(m_handle, interfaceNum, altSetting);
    if (rc < 0) {
        LOG_ERROR("UsbAudioDevice", "set_interface_alt_setting(%d, %d) failed: %s", interfaceNum, altSetting, libusb_error_name(rc));
        return false;
    }
    return true;
}

bool UsbAudioDevice::setSampleRate(uint32_t rate) {
    if (!m_handle || m_clockSourceIds.empty()) return false;
    
    // UAC2 CUR request to Clock Source
    uint8_t buf[4];
    buf[0] = (rate & 0xFF);
    buf[1] = ((rate >> 8) & 0xFF);
    buf[2] = ((rate >> 16) & 0xFF);
    buf[3] = ((rate >> 24) & 0xFF);

    // Apply to all clock sources 
    for (uint8_t cs : m_clockSourceIds) {
        int rc = libusb_control_transfer(
            m_handle, 
            LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
            UAC2_CUR, 
            (UAC2_CS_SAM_FREQ_CONTROL << 8), 
            (cs << 8) | m_acInterfaceNum, 
            buf, 4, 1000);
            
        if (rc < 0) {
            LOG_ERROR("UsbAudioDevice", "setSampleRate(%d) to CS ID %d failed: %s", rate, cs, libusb_error_name(rc));
        }
    }
    return true;
}

uint32_t UsbAudioDevice::getSampleRate() {
    if (!m_handle || m_clockSourceIds.empty()) return 0;

    uint8_t buf[4] = {0};
    int rc = libusb_control_transfer(
        m_handle, 
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
        UAC2_CUR, 
        (UAC2_CS_SAM_FREQ_CONTROL << 8), 
        (m_clockSourceIds[0] << 8) | m_acInterfaceNum, 
        buf, 4, 1000);

    if (rc < 4) {
        // Fallback to default
        return 48000;
    }

    return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
}
