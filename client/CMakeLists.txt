cmake_minimum_required(VERSION 3.16)
project(Perry_Client CXX)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

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
  src/mainwindow.cpp
  src/mainwindow.h
  src/mainwindow.ui
  src/utils.h
  src/utils.cpp
  src/workers/periodic_10.h
  src/workers/periodic_10.cpp
  src/workers/socket_reader.h
  src/workers/socket_reader.cpp
  src/workers/socket_sender.h
  src/workers/socket_sender.cpp
  src/widgets/chatMessageWidget.h
  src/widgets/chatMessageWidget.ui
)

#Link
target_link_libraries(perry_client PRIVATE Threads::Threads Qt6::Widgets)
target_include_directories(perry_client PRIVATE ../common)
