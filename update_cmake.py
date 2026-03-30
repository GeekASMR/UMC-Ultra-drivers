with open('d:/Autigravity/UMCasio/CMakeLists.txt', 'r', encoding='utf-8') as f:
    lines = f.readlines()
start = -1
end = -1
for i, line in enumerate(lines):
    if line.startswith('# Build as shared library (DLL)'):
        start = i
    if line.startswith('    COMMENT "Unregistering ASIO driver...'):
        end = i + 2
        break

new_content = """set(ASMR_TARGET_BRANDS
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
"""
lines = lines[:start] + [new_content] + lines[end:]
with open('d:/Autigravity/UMCasio/CMakeLists.txt', 'w', encoding='utf-8') as f:
    f.writelines(lines)
