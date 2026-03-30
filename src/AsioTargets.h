#pragma once
#include <windows.h>

// ASMRTOP V7.0 Universal Proxy Target Definition Mapping
// Controlled by Preprocessor Directives defined during build 

struct AsioBrandTarget {
    const char* searchKeyword; // To find original in registry
    const char* brandPrefix;   // e.g. "UMC" -> "UMC Ultra By ASMRTOP"
    const char* clsidStr;      // Registration string
    GUID clsid;                // COM matching GUID
};

#if defined(ASMRTOP_TARGET_BEHRINGER)
static const AsioBrandTarget g_CurrentTarget = {
    "UMC", "UMC Ultra By ASMRTOP", "{A1B2C3D4-E5F6-7890-ABCD-EF1234560000}",
    { 0xA1B2C3D4, 0xE5F6, 0x7890, { 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x00, 0x00 } }
};
#elif defined(ASMRTOP_TARGET_AUDIENT)
static const AsioBrandTarget g_CurrentTarget = {
    "Audient USB Audio ASIO", "Audient Ultra By ASMRTOP", "{A1B2C3D4-E5F6-7890-ABCD-EF1234560001}",
    { 0xA1B2C3D4, 0xE5F6, 0x7890, { 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x00, 0x01 } }
};
#elif defined(ASMRTOP_TARGET_SSL)
static const AsioBrandTarget g_CurrentTarget = {
    "Solid State Logic", "SSL Ultra By ASMRTOP", "{A1B2C3D4-E5F6-7890-ABCD-EF1234560002}",
    { 0xA1B2C3D4, 0xE5F6, 0x7890, { 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x00, 0x02 } }
};
#elif defined(ASMRTOP_TARGET_MACKIE)
static const AsioBrandTarget g_CurrentTarget = {
    "Mackie ASIO Driver", "Onyx Ultra By ASMRTOP", "{A1B2C3D4-E5F6-7890-ABCD-EF1234560005}",
    { 0xA1B2C3D4, 0xE5F6, 0x7890, { 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x00, 0x05 } }
};
#elif defined(ASMRTOP_TARGET_TASCAM)
static const AsioBrandTarget g_CurrentTarget = {
    "US-16x08 ASIO", "TASCAM Ultra By ASMRTOP", "{A1B2C3D4-E5F6-7890-ABCD-EF1234560006}",
    { 0xA1B2C3D4, 0xE5F6, 0x7890, { 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x00, 0x06 } }
};
#elif defined(ASMRTOP_TARGET_YAMAHA)
static const AsioBrandTarget g_CurrentTarget = {
    "Yamaha Steinberg USB ASIO", "Yamaha Ultra By ASMRTOP", "{A1B2C3D4-E5F6-7890-ABCD-EF123456000A}",
    { 0xA1B2C3D4, 0xE5F6, 0x7890, { 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x00, 0x0A } }
};
#elif defined(ASMRTOP_TARGET_MOTU)
static const AsioBrandTarget g_CurrentTarget = {
    "MOTU M Series", "MOTU Ultra By ASMRTOP", "{A1B2C3D4-E5F6-7890-ABCD-EF123456000C}",
    { 0xA1B2C3D4, 0xE5F6, 0x7890, { 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x00, 0x0C } }
};
#elif defined(ASMRTOP_TARGET_PRESONUS)
static const AsioBrandTarget g_CurrentTarget = {
    "Studio USB ASIO Driver", "PreSonus Ultra By ASMRTOP", "{A1B2C3D4-E5F6-7890-ABCD-EF123456000D}",
    { 0xA1B2C3D4, 0xE5F6, 0x7890, { 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x00, 0x0D } }
};
#elif defined(ASMRTOP_TARGET_FOCUSRITE)
static const AsioBrandTarget g_CurrentTarget = {
    "Focusrite USB ASIO", "Focusrite Ultra By ASMRTOP", "{A1B2C3D4-E5F6-7890-ABCD-EF123456000E}",
    { 0xA1B2C3D4, 0xE5F6, 0x7890, { 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x00, 0x0E } }
};
#elif defined(ASMRTOP_TARGET_ZOOM)
static const AsioBrandTarget g_CurrentTarget = {
    "ZOOM UAC-232 ASIO", "ZOOM Ultra By ASMRTOP", "{A1B2C3D4-E5F6-7890-ABCD-EF1234560012}",
    { 0xA1B2C3D4, 0xE5F6, 0x7890, { 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x00, 0x12 } }
};
#elif defined(ASMRTOP_TARGET_ART)
static const AsioBrandTarget g_CurrentTarget = {
    "ART USB Audio ASIO", "ART Ultra By ASMRTOP", "{A1B2C3D4-E5F6-7890-ABCD-EF1234560110}",
    { 0xA1B2C3D4, 0xE5F6, 0x7890, { 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x01, 0x10 } }
};
#else
// Default Generic Fallback for Testing
static const AsioBrandTarget g_CurrentTarget = {
    "TUSBAUDIO", "ASMRTOP Universal Ultra", "{A1B2C3D4-E5F6-7890-ABCD-EF1234560099}",
    { 0xA1B2C3D4, 0xE5F6, 0x7890, { 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x00, 0x99 } }
};
#endif
