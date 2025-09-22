cmake_minimum_required(VERSION 3.22)
project(wledqt LANGUAGES C CXX)  # <-- enable C and C++

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

# Qt's sane defaults
qt_standard_project_setup()

# ---- KissFFT (C) as a small static lib ----
add_library(kissfft STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/extern/kissfft/kiss_fft.c
  ${CMAKE_CURRENT_SOURCE_DIR}/extern/kissfft/tools/kiss_fftr.c
)

target_include_directories(kissfft PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/extern/kissfft
  ${CMAKE_CURRENT_SOURCE_DIR}/extern/kissfft/tools
)

# ---- Your executable ----
add_executable(wledqt
  src/main.cpp
  src/MainWindow.h   src/MainWindow.cpp
  src/AudioCapture.h src/AudioCapture.cpp
  src/AudioProcessor.h src/AudioProcessor.cpp
  src/BarsWidget.h   src/BarsWidget.cpp
  src/miniaudio_impl.cpp
  src/UdpSrSender.h
)

# Include paths for your sources and vendored headers
target_include_directories(wledqt PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/extern/miniaudio
)

# Link Qt + KissFFT
target_link_libraries(wledqt PRIVATE Qt6::Core Qt6::Widgets Qt6::Network kissfft)

# Link a couple Windows system libs (for WASAPI/COM via miniaudio)
if (WIN32)
  target_link_libraries(wledqt PRIVATE ole32 uuid)
endif()

# (Linux) libm if needed
if (UNIX AND NOT APPLE)
  target_link_libraries(wledqt PRIVATE m)
endif()

# Compiler warnings
if (MSVC)
  target_compile_options(wledqt PRIVATE /W4 /permissive-)
else()
  target_compile_options(wledqt PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Platform defines
if (WIN32)
  target_compile_definitions(wledqt PRIVATE WLEDQT_PLATFORM_WINDOWS)
endif()

# Deploy Qt DLLs on Windows
if (WIN32)
  add_custom_command(TARGET wledqt POST_BUILD
    COMMAND "${Qt6_DIR}/../../../bin/windeployqt" "$<TARGET_FILE:wledqt>" --no-quick-import
    COMMENT "Running windeployqt to stage Qt runtime dependencies"
  )
endif()

install(TARGETS wledqt RUNTIME DESTINATION .)
