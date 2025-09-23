cmake_minimum_required(VERSION 3.16)
project(Perry_Server CXX)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

# Fetch nanodbc
set(NANODBC_DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(NANODBC_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(NANODBC_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    nanodbc
    GIT_REPOSITORY https://github.com/nanodbc/nanodbc.git
    GIT_TAG        v2.14.0  # replace with the latest tag you want
)
FetchContent_MakeAvailable(nanodbc)

FetchContent_Declare(
    bcrypt
    GIT_REPOSITORY https://github.com/hilch/Bcrypt.cpp
    GIT_TAG        master  # replace with the latest tag you want
)
FetchContent_MakeAvailable(bcrypt)

# Find system ODBC library
find_package(ODBC REQUIRED)

# -pthreads
find_package(Threads REQUIRED)

# Build your executable
#add_executable(perry_server src/main.cpp)
add_executable(perry_server
  src/main.cpp
  ../common/common_data.h
  ../common/packets.h
  ../common/packets.cpp
  lib/DbManager.cpp
  lib/DbManager.h
)

# Link nanodbc and ODBC
target_link_libraries(perry_server PRIVATE nanodbc bcrypt ${ODBC_LIBRARIES})
target_include_directories(perry_server PRIVATE ../common lib ${ODBC_INCLUDE_DIRS})
target_link_libraries(perry_server PRIVATE Threads::Threads)
