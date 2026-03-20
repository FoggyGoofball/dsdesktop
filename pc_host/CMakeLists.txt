cmake_minimum_required(VERSION 3.20)
project(dsrd_host LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SOURCES src/*.cpp)

add_executable(dsrd_host ${SOURCES})

target_include_directories(dsrd_host PRIVATE
    include
    ../common
)

if(WIN32)
    target_compile_definitions(dsrd_host PRIVATE _WIN32)
    target_link_libraries(dsrd_host PRIVATE ws2_32 gdi32 ole32 uuid)
else()
    find_package(X11 REQUIRED)
    target_link_libraries(dsrd_host PRIVATE X11::X11 Threads::Threads)
    find_package(Threads REQUIRED)
endif()

option(USE_OPENSSL "Enable HMAC verification via OpenSSL" OFF)
if(USE_OPENSSL)
    find_package(OpenSSL REQUIRED)
    target_compile_definitions(dsrd_host PRIVATE HAS_OPENSSL)
    target_link_libraries(dsrd_host PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
