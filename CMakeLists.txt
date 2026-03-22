cmake_minimum_required(VERSION 3.16)
project(BehringerASIO VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    src/driver/AsioProxy.cpp
    src/driver/AsioProxy.h
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
)

# Link Windows libraries
target_link_libraries(BehringerASIO PRIVATE
    ole32
    oleaut32
    uuid
    setupapi
    avrt
    winmm
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
    
    # Set subsystem
    set_target_properties(BehringerASIO PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
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
