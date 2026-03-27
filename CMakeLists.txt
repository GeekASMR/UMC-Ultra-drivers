cmake_minimum_required(VERSION 3.16)
project(BehringerASIO VERSION 6.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options("/utf-8")
endif()

# Output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Source files
set(ASIO_SOURCES
    # ASIO SDK headers (header-only)
)

set(DRIVER_SOURCES
    src/driver/BehringerASIO.cpp
    src/driver/BehringerASIO.h
    src/driver/AudioBuffer.cpp
    src/driver/AudioBuffer.h
    src/driver/TusbAudioDirect.cpp
    src/driver/TusbAudioDirect.h
)

set(COM_SOURCES
    src/com/dllmain.cpp
    src/com/ClassFactory.cpp
    src/com/ClassFactory.h
)

set(UTILS_SOURCES
    src/utils/Logger.h
)

set(ASIO_HEADERS
    src/asio/asio.h
    src/asio/asiosys.h
    src/asio/iasiodrv.h
)

# Build as shared library (DLL)
add_library(BehringerASIO SHARED
    ${DRIVER_SOURCES}
    ${COM_SOURCES}
    ${UTILS_SOURCES}
    ${ASIO_HEADERS}
    BehringerASIO.def
    BehringerASIO.rc
)

# Include directories
target_include_directories(BehringerASIO PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/asio
    ${CMAKE_SOURCE_DIR}/libusb/VS2022/MS64/dll
    ${CMAKE_SOURCE_DIR}/libusb/include
)

# Link Windows libraries
target_link_libraries(BehringerASIO PRIVATE
    ole32
    oleaut32
    uuid
    setupapi
    avrt
    winmm
    winhttp
    ${CMAKE_SOURCE_DIR}/libusb/VS2022/MS64/dll/libusb-1.0.lib
)

# Compiler definitions
target_compile_definitions(BehringerASIO PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

# MSVC-specific settings
if(MSVC)
    target_compile_options(BehringerASIO PRIVATE
        /W3          # Warning level 3
        /O2          # Optimize for speed
        /GS-         # Disable security checks in hot paths
        /fp:fast     # Fast floating point
        /utf-8       # Source and execution charset UTF-8
    )
    
    # Set subsystem and static CRT
    set_target_properties(BehringerASIO PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

# MinGW-specific settings
if(MINGW)
    target_compile_options(BehringerASIO PRIVATE
        -O2
        -Wall
        -ffast-math
    )
endif()

# Output settings
set_target_properties(BehringerASIO PROPERTIES
    OUTPUT_NAME "BehringerASIO"
    SUFFIX ".dll"
    PREFIX ""
)

# Install target
install(TARGETS BehringerASIO
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)

# Print build info
message(STATUS "===========================================")
message(STATUS "  Behringer UMC ASIO Driver")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "===========================================")

# Registration helper target
add_custom_target(register
    COMMAND regsvr32 /s "$<TARGET_FILE:BehringerASIO>"
    DEPENDS BehringerASIO
    COMMENT "Registering ASIO driver..."
)

add_custom_target(unregister
    COMMAND regsvr32 /u /s "$<TARGET_FILE:BehringerASIO>"
    COMMENT "Unregistering ASIO driver..."
)

# === CLI Proxy for JS Web Frontend ===
add_executable(UMCControlProxy
    src/utils/UMCControlProxy.cpp
    src/driver/TusbAudioDirect.cpp
    src/driver/AudioBuffer.cpp
    src/driver/RoutingMatrix.cpp
)
target_include_directories(UMCControlProxy PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/libusb/include
)
target_compile_definitions(UMCControlProxy PRIVATE _CRT_SECURE_NO_WARNINGS)

target_link_libraries(UMCControlProxy PRIVATE
    ${CMAKE_SOURCE_DIR}/libusb/VS2022/MS64/dll/libusb-1.0.lib
)

install(TARGETS UMCControlProxy
    RUNTIME DESTINATION bin
)

# === IPC Monitor Diagnostic Tool ===
add_executable(ipc_monitor tools/ipc_monitor.cpp)
target_compile_definitions(ipc_monitor PRIVATE _CRT_SECURE_NO_WARNINGS)

# === IPC Quality Test Tool ===
add_executable(ipc_quality_test tools/ipc_quality_test.cpp)
target_compile_definitions(ipc_quality_test PRIVATE _CRT_SECURE_NO_WARNINGS)

# === SRC Quality Test (Offline) ===
add_executable(src_quality_test tools/src_quality_test.cpp)
target_include_directories(src_quality_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(src_quality_test PRIVATE _CRT_SECURE_NO_WARNINGS)

# === IPC Data Format Check ===
add_executable(ipc_data_check tools/ipc_data_check.cpp)
target_compile_definitions(ipc_data_check PRIVATE _CRT_SECURE_NO_WARNINGS)

# === Sine Wave Generator ===
add_executable(gen_sine_wav tools/gen_sine_wav.cpp)
target_compile_definitions(gen_sine_wav PRIVATE _CRT_SECURE_NO_WARNINGS)

# === Audio Diagnostic Tool ===
add_executable(audio_diag tools/audio_diag.cpp)
target_compile_definitions(audio_diag PRIVATE _CRT_SECURE_NO_WARNINGS)

# === Audio Diagnostic Tool v2 (Snapshot) ===
add_executable(audio_diag2 tools/audio_diag2.cpp)
target_compile_definitions(audio_diag2 PRIVATE _CRT_SECURE_NO_WARNINGS)

# === SRC Diagnostic Tool ===
add_executable(src_diag tools/src_diag.cpp)
target_compile_definitions(src_diag PRIVATE _CRT_SECURE_NO_WARNINGS)

# === IPC Output Test (Sine to REC_0) ===
add_executable(ipc_output_test tools/ipc_output_test.cpp)
target_compile_definitions(ipc_output_test PRIVATE _CRT_SECURE_NO_WARNINGS)

# === IPC Pop/Click Detector ===
add_executable(ipc_pop_detect tools/ipc_pop_detect.cpp)
target_compile_definitions(ipc_pop_detect PRIVATE _CRT_SECURE_NO_WARNINGS)

# === DirectSound Sine Test ===
add_executable(ds_sine_test tools/ds_sine_test.cpp)
target_compile_definitions(ds_sine_test PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(ds_sine_test dsound dxguid ole32)
