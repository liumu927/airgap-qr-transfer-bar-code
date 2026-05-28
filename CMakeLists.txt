cmake_minimum_required(VERSION 3.20)

project(AirGapQrTransfer
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_subdirectory(src/core)
add_subdirectory(src/qr)
add_subdirectory(src/app)

if(BUILD_TESTING)
    add_subdirectory(tests/app)
    add_subdirectory(tests/core)
    add_subdirectory(tests/qr)
endif()
