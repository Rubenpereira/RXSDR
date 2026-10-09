cmake_minimum_required(VERSION 3.16)
project(RXSDRNativo VERSION 1.0.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
#  RXSDR Nativo - receptor SDR que roda inteiro no Windows (7 SP1 ate 11)
#  Tela: Dear ImGui + Direct3D 9. Som: waveOut. Sem Qt, sem navegador.
#  CRT estatico (/MT): nao precisa de "Visual C++ Redistributable".
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(
    _WIN32_WINNT=0x0601
    WINVER=0x0601
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    UNICODE _UNICODE
)
if(MSVC)
    add_compile_options(/utf-8 /W3 /wd4244 /wd4267 /wd4305)
endif()

set(RTLSDR_INC "${CMAKE_CURRENT_SOURCE_DIR}/../project/third_party/librtlsdr/include")
set(SDRPLAY_INC "${CMAKE_CURRENT_SOURCE_DIR}/../project/third_party/sdrplay/inc")
set(RTLSDR_AVAILABLE 0)
if(EXISTS "${RTLSDR_INC}/rtl-sdr.h")
    set(RTLSDR_AVAILABLE 1)
endif()
set(SDRPLAY_AVAILABLE 0)
if(EXISTS "${SDRPLAY_INC}/sdrplay_api.h")
    set(SDRPLAY_AVAILABLE 1)
endif()

set(IMGUI third_party/imgui)
set(SOURCES
    src/main.cpp
    src/app/Radio.cpp
    src/app/SomNativo.cpp
    src/app/ProcAudio.cpp
    src/app/Decoders.cpp
    src/app/Dsd.cpp
    src/app/Tetra.cpp
    src/app/Externos.cpp
    src/decoders/CwCore.cpp
    src/decoders/RttyCore.cpp
    src/decoders/SitorBCore.cpp
    src/decoders/DscCore.cpp
    src/decoders/AleCore.cpp
    src/decoders/AnaliseCore.cpp
    src/decoders/SstvCore.cpp
    src/decoders/WefaxCore.cpp
    src/decoders/PactorCore.cpp
    src/ui/Ui.cpp
    src/ui/Idioma.cpp
    src/dsp/Demodulator.cpp
    src/dsp/DemodAM.cpp
    src/dsp/DemodFM.cpp
    src/dsp/DemodSSB.cpp
    src/dsp/DemodCW.cpp
    src/dsp/FftProcessor.cpp
    src/dsp/Filters.cpp
    src/sdr/DeviceFactory.cpp
    src/sdr/RtlSdrDevice.cpp
    src/sdr/RtlTcpClient.cpp
    src/sdr/SdrplayDevice.cpp
    src/util/Logger.cpp
    ${IMGUI}/imgui.cpp
    ${IMGUI}/imgui_draw.cpp
    ${IMGUI}/imgui_tables.cpp
    ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_dx9.cpp
    ${IMGUI}/backends/imgui_impl_win32.cpp
    src/rxsdr.rc
)

add_executable(RXSDR WIN32 ${SOURCES})
target_include_directories(RXSDR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/${IMGUI}
)
if(RTLSDR_AVAILABLE)
    target_include_directories(RXSDR PRIVATE ${RTLSDR_INC})
endif()
if(SDRPLAY_AVAILABLE)
    target_include_directories(RXSDR PRIVATE ${SDRPLAY_INC})
endif()
target_compile_definitions(RXSDR PRIVATE
    RTLSDR_AVAILABLE=${RTLSDR_AVAILABLE}
    SDRPLAY_AVAILABLE=${SDRPLAY_AVAILABLE}
)
target_link_libraries(RXSDR PRIVATE d3d9 winmm ws2_32 shell32 advapi32 user32 kernel32 gdi32 dwmapi)

if(RTLSDR_AVAILABLE)
    foreach(lib rtlsdr.lib librtlsdr.lib)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../project/third_party/librtlsdr/lib/${lib}")
            target_link_libraries(RXSDR PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../project/third_party/librtlsdr/lib/${lib}")
            break()
        endif()
    endforeach()
endif()
