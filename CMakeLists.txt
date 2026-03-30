cmake_minimum_required(VERSION 3.16)
project(BehringerASIO VERSION 7.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options("/utf-8")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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
    src/driver/AsioProxy.cpp
    src/driver/AsioProxy.h
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

set(ASMR_TARGET_BRANDS
    BEHRINGER
    AUDIENT
    SSL
    MACKIE
    TASCAM
    YAMAHA
    MOTU
    PRESONUS
    FOCUSRITE
    ZOOM
    ART
    ROLAND
    MAUDIO
    UAD_VOLT
)

foreach(BRAND IN LISTS ASMR_TARGET_BRANDS)
    set(TARGET_NAME "ASMRTOP_${BRAND}")

    add_library(${TARGET_NAME} SHARED
        ${DRIVER_SOURCES}
        ${COM_SOURCES}
        ${UTILS_SOURCES}
        ${ASIO_HEADERS}
        BehringerASIO.def
        BehringerASIO.rc
    )

    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/asio
        ${CMAKE_SOURCE_DIR}/libusb/VS2022/MS64/dll
        ${CMAKE_SOURCE_DIR}/libusb/include
    )

    target_link_libraries(${TARGET_NAME} PRIVATE
        ole32 oleaut32 uuid setupapi avrt winmm winhttp
        ${CMAKE_SOURCE_DIR}/libusb/VS2022/MS64/dll/libusb-1.0.lib
    )

    target_compile_definitions(${TARGET_NAME} PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
        ASMRTOP_TARGET_${BRAND}
    )

    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE
            /W3 /O2 /GS- /fp:fast /utf-8
        )
        set_target_properties(${TARGET_NAME} PROPERTIES
            LINK_FLAGS "/SUBSYSTEM:WINDOWS"
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()

    if(MINGW)
        target_compile_options(${TARGET_NAME} PRIVATE -O2 -Wall -ffast-math)
    endif()

    set_target_properties(${TARGET_NAME} PROPERTIES
        OUTPUT_NAME "${TARGET_NAME}"
        SUFFIX ".dll"
        PREFIX ""
    )

    install(TARGETS ${TARGET_NAME} RUNTIME DESTINATION bin LIBRARY DESTINATION lib)
endforeach()

message(STATUS "===========================================")
message(STATUS "  ASMRTOP Universal Defense Fleet Configured")
message(STATUS "  Total Active Target DLLs: 11")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "===========================================")

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

# === ASIO Ultra Control Panel ===
add_executable(ASIOUltraControlPanel WIN32 src/gui/UMCControlPanel.cpp src/gui/OptimizerManager.cpp src/license/LicenseManager.h src/gui/app.rc)
target_link_libraries(ASIOUltraControlPanel PRIVATE shlwapi shell32 user32 gdi32 comctl32 wininet winhttp)
set_target_properties(ASIOUltraControlPanel PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")


# === ASIO Buffer Switch Stress Test ===
add_executable(asio_switch_test tools/asio_switch_test.cpp)
target_compile_definitions(asio_switch_test PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(asio_switch_test ole32)
