cmake_minimum_required(VERSION 3.16)
project(Perry_Client CXX)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

# Fetch Yaml-cpp
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG 0.8.0 # Can be a tag (yaml-cpp-x.x.x), a commit hash, or a branch name (master)
)
FetchContent_MakeAvailable(yaml-cpp)

# Local libsoundio
add_subdirectory(lib/libsoundio)

# QT6
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# -pthreads
find_package(Threads REQUIRED)

qt_standard_project_setup()

# Build your executable
qt_add_executable(perry_client
  src/main.cpp
  ../common/common_data.h
  ../common/packets.cpp
  ../common/packets.h
  ../common/crossSockets.cpp
  ../common/crossSockets.h
  ../common/logger.h
  ../common/logger.cpp
  src/mainwindow.cpp
  src/mainwindow.h
  src/mainwindow.ui
  src/utils.h
  src/utils.cpp
  src/config.h
  src/config.cpp
  src/workers/periodic_10.h
  src/workers/periodic_10.cpp
  src/workers/socket_reader.h
  src/workers/socket_reader.cpp
  src/workers/socket_sender.h
  src/workers/socket_sender.cpp
  src/workers/voice_chat.h
  src/workers/voice_chat.cpp
  src/widgets/chatMessageWidget.h
  src/widgets/chatMessageWidget.ui
)

#Link
target_link_libraries(perry_client PRIVATE Threads::Threads Qt6::Widgets libsoundio_shared yaml-cpp::yaml-cpp)
target_include_directories(perry_client PRIVATE ../common ${libsoundio_SOURCE_DIR})

if (WIN32)
    target_link_libraries(perry_client PRIVATE ws2_32)
endif()