cmake_minimum_required(VERSION 3.16)
project(RXSDR VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── CRT estatico (/MT) — sem vcruntime DLL ─────────────────────────────────
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ── Alvo Windows 7 SP1 ─────────────────────────────────────────────────────
add_compile_definitions(
    _WIN32_WINNT=0x0601
    WINVER=0x0601
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

# ── RTL-SDR ────────────────────────────────────────────────────────────────
set(RTLSDR_INC "${CMAKE_CURRENT_SOURCE_DIR}/../project/third_party/librtlsdr/include")
if(EXISTS "${RTLSDR_INC}/rtl-sdr.h")
    set(RTLSDR_AVAILABLE 1)
    message(STATUS "RTL-SDR: ${RTLSDR_INC}")
else()
    set(RTLSDR_AVAILABLE 0)
    message(WARNING "RTL-SDR nao encontrado em ${RTLSDR_INC}")
endif()

# ── SDRplay (LoadLibrary dinamico) ─────────────────────────────────────────
set(SDRPLAY_INC "${CMAKE_CURRENT_SOURCE_DIR}/../project/third_party/sdrplay/inc")
if(EXISTS "${SDRPLAY_INC}/sdrplay_api.h")
    set(SDRPLAY_AVAILABLE 1)
    message(STATUS "SDRplay: ${SDRPLAY_INC}")
else()
    set(SDRPLAY_AVAILABLE 0)
    message(WARNING "SDRplay nao encontrado em ${SDRPLAY_INC}")
endif()

message(STATUS "=== RXSDR Win7 Build ===")
message(STATUS "  RTL-SDR: ${RTLSDR_AVAILABLE}")
message(STATUS "  SDRplay: ${SDRPLAY_AVAILABLE}")
message(STATUS "  CRT:     estatico (/MT) — sem vcruntime dll")
message(STATUS "  Target:  Windows 7 SP1+")

# ── Fontes ─────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/app/TrayController.cpp
    src/app/Config.h          # header-only, sem .cpp
    src/dsp/Demodulator.cpp
    src/dsp/FftProcessor.cpp
    src/dsp/Filters.cpp
    src/dsp/DemodAM.cpp
    src/dsp/DemodFM.cpp
    src/dsp/DemodSSB.cpp
    src/dsp/DemodCW.cpp
    src/sdr/DeviceFactory.cpp
    src/sdr/RtlSdrDevice.cpp
    src/sdr/RtlTcpClient.cpp
    src/sdr/SdrplayDevice.cpp
    src/server/HttpServer.cpp
    src/server/WsServer.cpp
    src/server/RestApi.cpp
    src/util/Logger.cpp
)

set(RXSDR_RC "")
if(WIN32)
    set(RXSDR_RC "${CMAKE_CURRENT_SOURCE_DIR}/src/rxsdr.rc")
endif()

add_executable(RXSDR WIN32 ${SOURCES} ${RXSDR_RC})

# ── Includes ───────────────────────────────────────────────────────────────
target_include_directories(RXSDR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(RTLSDR_AVAILABLE)
    target_include_directories(RXSDR PRIVATE ${RTLSDR_INC})
endif()
if(SDRPLAY_AVAILABLE)
    target_include_directories(RXSDR PRIVATE ${SDRPLAY_INC})
endif()

# ── Defines de features ────────────────────────────────────────────────────
target_compile_definitions(RXSDR PRIVATE
    RTLSDR_AVAILABLE=${RTLSDR_AVAILABLE}
    SDRPLAY_AVAILABLE=${SDRPLAY_AVAILABLE}
)

# ── Bibliotecas Win32 ──────────────────────────────────────────────────────
target_link_libraries(RXSDR PRIVATE
    ws2_32
    shell32
    advapi32
    user32
    kernel32
    gdi32
)

# ── RTL-SDR lib ────────────────────────────────────────────────────────────
if(RTLSDR_AVAILABLE)
    set(RTLSDR_LIB
        "${CMAKE_CURRENT_SOURCE_DIR}/../project/third_party/librtlsdr/lib/rtlsdr.lib")
    if(EXISTS "${RTLSDR_LIB}")
        target_link_libraries(RXSDR PRIVATE "${RTLSDR_LIB}")
    else()
        # Tenta librtlsdr.lib
        set(RTLSDR_LIB2
            "${CMAKE_CURRENT_SOURCE_DIR}/../project/third_party/librtlsdr/lib/librtlsdr.lib")
        if(EXISTS "${RTLSDR_LIB2}")
            target_link_libraries(RXSDR PRIVATE "${RTLSDR_LIB2}")
        else()
            message(WARNING "RTL-SDR .lib nao encontrado — build pode falhar no link")
        endif()
    endif()
endif()

# ── POST_BUILD: copia pasta web e icone ───────────────────────────────────
add_custom_command(TARGET RXSDR POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../project/web"
        "$<TARGET_FILE_DIR:RXSDR>/web"
    COMMENT "Copiando pasta web..."
)

# Copia icone se existir
set(ICO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../project/build/app.ico")
if(NOT EXISTS "${ICO_SRC}")
    set(ICO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../project/installer/app.ico")
endif()
if(EXISTS "${ICO_SRC}")
    add_custom_command(TARGET RXSDR POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ICO_SRC}"
            "$<TARGET_FILE_DIR:RXSDR>/app.ico"
        COMMENT "Copiando app.ico..."
    )
endif()
