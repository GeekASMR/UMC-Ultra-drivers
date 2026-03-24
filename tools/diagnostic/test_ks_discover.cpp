/*
 * test_ks_discover.cpp - Find KS audio filter devices for UMC 1820
 * This verifies we can access the kernel streaming interface
 */

#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <ks.h>
#include <ksmedia.h>
#include <stdio.h>

#pragma comment(lib, "setupapi.lib")

// KSCATEGORY_AUDIO  {6994AD04-93EF-11D0-A3CC-00A0C9223196}
// KSCATEGORY_RENDER {65E8773E-8F56-11D0-A3B9-00A0C9223196}
// KSCATEGORY_CAPTURE {65E8773D-8F56-11D0-A3B9-00A0C9223196}

void enumerateCategory(const char* name, const GUID& category) {
    printf("\n=== %s ===\n", name);

    HDEVINFO devInfo = SetupDiGetClassDevsW(&category, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) {
        printf("  SetupDiGetClassDevs failed: %d\n", GetLastError());
        return;
    }

    SP_DEVICE_INTERFACE_DATA ifData;
    ifData.cbSize = sizeof(ifData);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &category, i, &ifData); i++) {
        // Get detail size
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &size, nullptr);

        auto* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)malloc(size);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(devInfoData);

        if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, size, nullptr, &devInfoData)) {
            // Check if Behringer (VID_1397)
            bool isBehringer = (wcsstr(detail->DevicePath, L"vid_1397") != nullptr ||
                               wcsstr(detail->DevicePath, L"VID_1397") != nullptr);

            if (isBehringer) {
                printf("  [%d] *** BEHRINGER ***\n", i);
            } else {
                printf("  [%d]\n", i);
            }
            printf("      Path: %ls\n", detail->DevicePath);

            // Try to open the device
            if (isBehringer) {
                HANDLE hFilter = CreateFileW(detail->DevicePath,
                    GENERIC_READ | GENERIC_WRITE,
                    0, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                    nullptr);

                if (hFilter != INVALID_HANDLE_VALUE) {
                    printf("      -> OPENED SUCCESSFULLY!\n");

                    // Query pin count
                    KSPROPERTY prop;
                    prop.Set = KSPROPSETID_Pin;
                    prop.Id = KSPROPERTY_PIN_CTYPES;
                    prop.Flags = KSPROPERTY_TYPE_GET;

                    ULONG pinCount = 0;
                    DWORD bytesReturned = 0;

                    if (DeviceIoControl(hFilter, IOCTL_KS_PROPERTY,
                        &prop, sizeof(prop), &pinCount, sizeof(pinCount),
                        &bytesReturned, nullptr)) {
                        printf("      Pin factories: %d\n", pinCount);

                        // Query each pin's data flow direction
                        for (ULONG p = 0; p < pinCount; p++) {
                            KSP_PIN pinProp;
                            pinProp.Property.Set = KSPROPSETID_Pin;
                            pinProp.Property.Id = KSPROPERTY_PIN_DATAFLOW;
                            pinProp.Property.Flags = KSPROPERTY_TYPE_GET;
                            pinProp.PinId = p;

                            KSPIN_DATAFLOW dataFlow;
                            if (DeviceIoControl(hFilter, IOCTL_KS_PROPERTY,
                                &pinProp, sizeof(pinProp), &dataFlow, sizeof(dataFlow),
                                &bytesReturned, nullptr)) {
                                const char* flowStr = "Unknown";
                                if (dataFlow == KSPIN_DATAFLOW_IN) flowStr = "IN (render/sink)";
                                if (dataFlow == KSPIN_DATAFLOW_OUT) flowStr = "OUT (capture/source)";
                                printf("      Pin[%d]: %s\n", p, flowStr);
                            }

                            // Query communication type
                            pinProp.Property.Id = KSPROPERTY_PIN_COMMUNICATION;
                            KSPIN_COMMUNICATION comm;
                            if (DeviceIoControl(hFilter, IOCTL_KS_PROPERTY,
                                &pinProp, sizeof(pinProp), &comm, sizeof(comm),
                                &bytesReturned, nullptr)) {
                                const char* commStr = "None";
                                if (comm == KSPIN_COMMUNICATION_SINK) commStr = "Sink";
                                if (comm == KSPIN_COMMUNICATION_SOURCE) commStr = "Source";
                                if (comm == KSPIN_COMMUNICATION_BOTH) commStr = "Both";
                                if (comm == KSPIN_COMMUNICATION_BRIDGE) commStr = "Bridge";
                                printf("              Comm: %s\n", commStr);
                            }
                        }
                    } else {
                        printf("      Pin query failed: %d\n", GetLastError());
                    }

                    CloseHandle(hFilter);
                } else {
                    printf("      -> Open failed: %d\n", GetLastError());
                }
            }
        }
        free(detail);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
}

int main() {
    printf("=== KS Audio Device Discovery ===\n");

    enumerateCategory("KSCATEGORY_AUDIO",   KSCATEGORY_AUDIO);
    enumerateCategory("KSCATEGORY_RENDER",  KSCATEGORY_RENDER);
    enumerateCategory("KSCATEGORY_CAPTURE", KSCATEGORY_CAPTURE);

    printf("\n=== Done ===\n");
    return 0;
}
