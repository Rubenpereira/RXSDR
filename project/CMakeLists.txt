cmake_minimum_required(VERSION 3.21)
project(RXSDR VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR "No Windows, use toolchain MSVC (cl.exe).")
endif()

if(MSVC)
    add_compile_options(/W4 /utf-8 /MP)
    add_compile_definitions(_WIN32_WINNT=0x0A00 NOMINMAX CREATE_NO_WINDOW=0x08000000)
endif()

option(MASDR_ENABLE_RTLSDR  "Enable RTL-SDR USB via librtlsdr" ON)
option(MASDR_ENABLE_SDRPLAY "Enable SDRplay API v3" ON)
option(RXSDR_HEADLESS       "Build in headless mode (no GUI/Tray)" OFF)

if(RXSDR_HEADLESS)
    add_compile_definitions(RXSDR_HEADLESS)
endif()


# ----- Qt6 -----
find_package(Qt6 6.0 REQUIRED COMPONENTS
    Core Gui Widgets Network WebSockets Multimedia HttpServer Concurrent)

set(SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/app/Config.cpp
    src/app/TrayController.cpp
    src/server/HttpServer.cpp
    src/server/WsServer.cpp
    src/server/RestApi.cpp
    src/sdr/DeviceFactory.cpp
    src/sdr/RtlSdrDevice.cpp
    src/sdr/RtlTcpClient.cpp
    src/sdr/SdrplayDevice.cpp
    src/dsp/FftProcessor.cpp
    src/dsp/Demodulator.cpp
    src/dsp/DemodAM.cpp
    src/dsp/DemodFM.cpp
    src/dsp/DemodSSB.cpp
    src/dsp/DemodCW.cpp
    src/dsp/Filters.cpp
    src/dsp/AudioOutput.cpp
    src/decoders/AisCatcherManager.cpp
    src/decoders/AcarsDecoManager.cpp
    src/decoders/DsdManager.cpp
    src/decoders/AprsManager.cpp
    src/decoders/AprsIsClient.cpp
    src/decoders/SitorBManager.cpp
    src/decoders/PactorManager.cpp
    src/decoders/DscManager.cpp
    src/decoders/SelcalManager.cpp
    src/decoders/TetraManager.cpp
    src/util/Logger.cpp
)

set(HEADERS
    src/app/Application.h
    src/app/Config.h
    src/app/TrayController.h
    src/server/HttpServer.h
    src/server/WsServer.h
    src/server/RestApi.h
    src/sdr/ISdrDevice.h
    src/sdr/DeviceFactory.h
    src/sdr/RtlSdrDevice.h
    src/sdr/RtlTcpClient.h
    src/sdr/SdrplayDevice.h
    src/dsp/RingBuffer.h
    src/dsp/FftProcessor.h
    src/dsp/Demodulator.h
    src/dsp/DemodAM.h
    src/dsp/DemodFM.h
    src/dsp/DemodSSB.h
    src/dsp/DemodCW.h
    src/dsp/Filters.h
    src/dsp/AudioOutput.h
    src/decoders/IDecoder.h
    src/decoders/AisCatcherManager.h
    src/decoders/AcarsDecoManager.h
    src/decoders/DsdManager.h
    src/decoders/AprsManager.h
    src/decoders/AprsIsClient.h
    src/decoders/SitorBManager.h
    src/decoders/PactorManager.h
    src/decoders/DscManager.h
    src/decoders/SelcalManager.h
    src/decoders/TetraManager.h
    src/util/Logger.h
    src/util/Timing.h
)

add_executable(RXSDR WIN32 ${SOURCES} ${HEADERS})

target_include_directories(RXSDR PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(RXSDR PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
    Qt6::Network Qt6::WebSockets
    Qt6::Multimedia Qt6::HttpServer Qt6::Concurrent
)

# ----- RTL-SDR (opcional, auto-detect) -----
# Limpa o cache de busca para que um SDK recém-adicionado seja detectado numa re-configuração.
unset(RTLSDR_INCLUDE_DIR CACHE)
unset(RTLSDR_LIBRARY     CACHE)

set(MASDR_RTLSDR_ENABLED 0)
if(MASDR_ENABLE_RTLSDR)
    set(_rtlsdr_roots
        "${CMAKE_SOURCE_DIR}/third_party/librtlsdr"
        "$ENV{RTLSDR_ROOT}"
    )
    if(WIN32)
        find_path(RTLSDR_INCLUDE_DIR
            NAMES rtl-sdr.h
            PATH_SUFFIXES include inc
            PATHS ${_rtlsdr_roots}
            NO_DEFAULT_PATH)
        find_library(RTLSDR_LIBRARY
            NAMES rtlsdr rtlsdr_static
            PATH_SUFFIXES lib lib64 x64
            PATHS ${_rtlsdr_roots}
            NO_DEFAULT_PATH)
    else()
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(RTLSDR librtlsdr)
        endif()
        if(RTLSDR_FOUND)
            set(RTLSDR_INCLUDE_DIR ${RTLSDR_INCLUDE_DIRS})
            set(RTLSDR_LIBRARY ${RTLSDR_LIBRARIES})
        else()
            find_path(RTLSDR_INCLUDE_DIR NAMES rtl-sdr.h)
            find_library(RTLSDR_LIBRARY NAMES rtlsdr)
        endif()
    endif()

    if(RTLSDR_INCLUDE_DIR AND RTLSDR_LIBRARY)
        set(MASDR_RTLSDR_ENABLED 1)
        target_include_directories(RXSDR PRIVATE "${RTLSDR_INCLUDE_DIR}")
        target_link_libraries(RXSDR PRIVATE "${RTLSDR_LIBRARY}")
        message(STATUS "RTL-SDR include: ${RTLSDR_INCLUDE_DIR}")
        message(STATUS "RTL-SDR library: ${RTLSDR_LIBRARY}")

        if(WIN32)
            # Copia DLLs de runtime do RTL-SDR para junto do executável
            set(_rtlsdr_lib_dir "${CMAKE_SOURCE_DIR}/third_party/librtlsdr/lib")
            foreach(_dll rtlsdr.dll librtlsdr.dll libusb-1.0.dll pthreadVC2.dll msvcr100.dll)
                if(EXISTS "${_rtlsdr_lib_dir}/${_dll}")
                    add_custom_command(TARGET RXSDR POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                "${_rtlsdr_lib_dir}/${_dll}"
                                "$<TARGET_FILE_DIR:RXSDR>/${_dll}"
                        COMMENT "Copiando ${_dll}...")
                endif()
            endforeach()
        endif()
    else()
        message(WARNING "RTL-SDR SDK não encontrado — suporte USB desabilitado.")
        message(STATUS  "  Procurado em: ${_rtlsdr_roots}")
    endif()
endif()
target_compile_definitions(RXSDR PRIVATE RTLSDR_AVAILABLE=${MASDR_RTLSDR_ENABLED})
message(STATUS "RTL-SDR support: ${MASDR_RTLSDR_ENABLED}")

# ----- SDRplay (carregamento dinâmico — não precisa de .lib) -----
# O header sdrplay_api.h é bundled em third_party/sdrplay/inc/
# A DLL (sdrplay_api.dll) é carregada em runtime via LoadLibrary.
# O usuário com hardware SDRplay deve instalar: https://www.sdrplay.com/api/
set(MASDR_SDRPLAY_ENABLED 0)
if(MASDR_ENABLE_SDRPLAY)
    set(_sdrplay_inc_search
        "${CMAKE_SOURCE_DIR}/third_party/sdrplay/inc"   # bundled (prioridade)
        "${CMAKE_SOURCE_DIR}/third_party/sdrplay/include"
        "$ENV{SDRPLAY_API_ROOT}/inc"
        "C:/Program Files/SDRplay/API/inc"
        "C:/Program Files/SDRplay/API/include"
    )
    find_path(SDRPLAY_INCLUDE_DIR
        NAMES sdrplay_api.h
        PATHS ${_sdrplay_inc_search}
        NO_DEFAULT_PATH)
    if(SDRPLAY_INCLUDE_DIR)
        set(MASDR_SDRPLAY_ENABLED 1)
        target_include_directories(RXSDR PRIVATE "${SDRPLAY_INCLUDE_DIR}")
        # Sem target_link_libraries — usamos LoadLibrary em runtime
    endif()
endif()
target_compile_definitions(RXSDR PRIVATE SDRPLAY_AVAILABLE=${MASDR_SDRPLAY_ENABLED})
message(STATUS "SDRplay support: ${MASDR_SDRPLAY_ENABLED} (dynamic loading via LoadLibrary)")

# ----- Copia pasta web/ para junto do executável -----
add_custom_command(TARGET RXSDR POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/web
            $<TARGET_FILE_DIR:RXSDR>/web
)

# ----- Instalação -----
# ----- Copia decoders externos para junto do executavel -----
add_custom_command(TARGET RXSDR POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/decoders
            $<TARGET_FILE_DIR:RXSDR>/decoders
)

install(TARGETS RXSDR RUNTIME DESTINATION bin)
install(DIRECTORY web/ DESTINATION bin/web)
install(DIRECTORY decoders/ DESTINATION bin/decoders)

# ----- Testes (opcional) -----
enable_testing()
# add_subdirectory(tests)
