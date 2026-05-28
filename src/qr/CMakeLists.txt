add_library(airgap_qr
    qr_adapter.cpp
    qr_cimbar_adapter.cpp
    qr_libqrencode_adapter.cpp
    qr_mock_adapter.cpp
    qr_zxing_adapter.cpp
)

target_include_directories(airgap_qr
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(airgap_qr PUBLIC airgap_core)

target_compile_features(airgap_qr PUBLIC cxx_std_20)

option(AIRGAP_ENABLE_CIMBAR "Enable experimental libcimbar high-density color barcode support." OFF)
if(AIRGAP_ENABLE_CIMBAR)
    set(AIRGAP_CIMBAR_ROOT "${CMAKE_SOURCE_DIR}/external/libcimbar" CACHE PATH "Path to the libcimbar source tree.")
    if(NOT EXISTS "${AIRGAP_CIMBAR_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR "AIRGAP_ENABLE_CIMBAR is ON, but libcimbar was not found at ${AIRGAP_CIMBAR_ROOT}. Run: git submodule update --init --recursive")
    endif()

    find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc photo calib3d)
    set(DISABLE_TESTS TRUE CACHE BOOL "Disable libcimbar tests inside the AirGap build." FORCE)
    set(OPENCV_LIBS ${OpenCV_LIBS})
    set(CPPFILESYSTEM "")
    add_compile_definitions(LIBCIMBAR_PROJECT_ROOT="${AIRGAP_CIMBAR_ROOT}")
    if(MSVC)
        add_compile_options(/utf-8 /FI "iso646.h")
    endif()
    include_directories(
        ${OpenCV_INCLUDE_DIRS}
        "${AIRGAP_CIMBAR_ROOT}/src/lib"
        "${AIRGAP_CIMBAR_ROOT}/src/third_party_lib"
    )

    add_subdirectory("${AIRGAP_CIMBAR_ROOT}/src/third_party_lib/base91" "${CMAKE_BINARY_DIR}/libcimbar/base91")
    add_subdirectory("${AIRGAP_CIMBAR_ROOT}/src/third_party_lib/intx" "${CMAKE_BINARY_DIR}/libcimbar/intx")
    add_subdirectory("${AIRGAP_CIMBAR_ROOT}/src/third_party_lib/libpopcnt" "${CMAKE_BINARY_DIR}/libcimbar/libpopcnt")
    # libcimbar's libcorrect enables ASAN for Debug, which is not linked by Qt Android debug APKs.
    set(_airgap_outer_build_type "${CMAKE_BUILD_TYPE}")
    if(ANDROID AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_BUILD_TYPE RelWithDebInfo)
    endif()
    add_subdirectory("${AIRGAP_CIMBAR_ROOT}/src/third_party_lib/libcorrect" "${CMAKE_BINARY_DIR}/libcimbar/libcorrect")
    set(CMAKE_BUILD_TYPE "${_airgap_outer_build_type}")
    unset(_airgap_outer_build_type)
    add_subdirectory("${AIRGAP_CIMBAR_ROOT}/src/third_party_lib/wirehair" "${CMAKE_BINARY_DIR}/libcimbar/wirehair")
    add_subdirectory("${AIRGAP_CIMBAR_ROOT}/src/third_party_lib/zstd" "${CMAKE_BINARY_DIR}/libcimbar/zstd")
    add_subdirectory("${AIRGAP_CIMBAR_ROOT}/src/lib/serialize" "${CMAKE_BINARY_DIR}/libcimbar/serialize")
    add_subdirectory("${AIRGAP_CIMBAR_ROOT}/src/lib/util" "${CMAKE_BINARY_DIR}/libcimbar/util")
    add_subdirectory("${AIRGAP_CIMBAR_ROOT}/src/lib/bit_file" "${CMAKE_BINARY_DIR}/libcimbar/bit_file")
    add_subdirectory("${AIRGAP_CIMBAR_ROOT}/src/lib/compression" "${CMAKE_BINARY_DIR}/libcimbar/compression")
    add_subdirectory("${AIRGAP_CIMBAR_ROOT}/src/lib/fountain" "${CMAKE_BINARY_DIR}/libcimbar/fountain")
    add_subdirectory("${AIRGAP_CIMBAR_ROOT}/src/lib/cimb_translator" "${CMAKE_BINARY_DIR}/libcimbar/cimb_translator")
    add_subdirectory("${AIRGAP_CIMBAR_ROOT}/src/lib/extractor" "${CMAKE_BINARY_DIR}/libcimbar/extractor")

    add_library(airgap_cimbar_vendor STATIC
        "${AIRGAP_CIMBAR_ROOT}/src/lib/cimbar_js/cimbar_recv_js.cpp"
    )
    target_include_directories(airgap_cimbar_vendor
        PUBLIC
            "${AIRGAP_CIMBAR_ROOT}/src/lib"
            "${AIRGAP_CIMBAR_ROOT}/src/third_party_lib"
            ${OpenCV_INCLUDE_DIRS}
    )
    target_link_libraries(airgap_cimbar_vendor
        PUBLIC
            cimb_translator
            extractor
            correct_static
            wirehair
            zstd
            ${OpenCV_LIBS}
    )
    target_compile_features(airgap_cimbar_vendor PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(airgap_cimbar_vendor PRIVATE /utf-8)
        target_compile_options(airgap_qr PRIVATE /utf-8)
    endif()
    target_compile_definitions(airgap_qr PUBLIC AIRGAP_HAS_CIMBAR=1)
    target_link_libraries(airgap_qr PRIVATE airgap_cimbar_vendor)
else()
    target_compile_definitions(airgap_qr PUBLIC AIRGAP_HAS_CIMBAR=0)
endif()

set(AIRGAP_QR_DEP_ROOT "" CACHE PATH "Optional root path containing QR adapter dependencies for cross builds.")
if(AIRGAP_QR_DEP_ROOT)
    if(NOT ZXing_DIR AND EXISTS "${AIRGAP_QR_DEP_ROOT}/share/ZXing/ZXingConfig.cmake")
        set(ZXing_DIR "${AIRGAP_QR_DEP_ROOT}/share/ZXing" CACHE PATH "Path to ZXingConfig.cmake" FORCE)
    endif()
    find_path(QRENCODE_INCLUDE_DIR NAMES qrencode.h
        PATHS "${AIRGAP_QR_DEP_ROOT}/include"
        NO_DEFAULT_PATH
        NO_CMAKE_FIND_ROOT_PATH
    )
    find_library(QRENCODE_LIBRARY_RELEASE NAMES qrencode
        PATHS "${AIRGAP_QR_DEP_ROOT}/lib"
        NO_DEFAULT_PATH
        NO_CMAKE_FIND_ROOT_PATH
    )
    find_library(QRENCODE_LIBRARY_DEBUG NAMES qrencoded qrencode
        PATHS "${AIRGAP_QR_DEP_ROOT}/debug/lib" "${AIRGAP_QR_DEP_ROOT}/lib"
        NO_DEFAULT_PATH
        NO_CMAKE_FIND_ROOT_PATH
    )
else()
    find_path(QRENCODE_INCLUDE_DIR NAMES qrencode.h)
    find_library(QRENCODE_LIBRARY_RELEASE NAMES qrencode)
    find_library(QRENCODE_LIBRARY_DEBUG NAMES qrencoded qrencode)
endif()

if(QRENCODE_INCLUDE_DIR AND QRENCODE_LIBRARY_RELEASE)
    target_compile_definitions(airgap_qr PUBLIC AIRGAP_HAS_LIBQRENCODE=1)
    target_include_directories(airgap_qr PRIVATE ${QRENCODE_INCLUDE_DIR})
    target_link_libraries(airgap_qr
        PRIVATE
            optimized ${QRENCODE_LIBRARY_RELEASE}
            debug ${QRENCODE_LIBRARY_DEBUG}
    )
else()
    target_compile_definitions(airgap_qr PUBLIC AIRGAP_HAS_LIBQRENCODE=0)
endif()

find_package(ZXing CONFIG QUIET)
set(AIRGAP_ZXING_TARGETS)
if(TARGET ZXing::Core)
    list(APPEND AIRGAP_ZXING_TARGETS ZXing::Core)
endif()
if(TARGET ZXing::ZXing)
    list(APPEND AIRGAP_ZXING_TARGETS ZXing::ZXing)
endif()

if(AIRGAP_ZXING_TARGETS)
    target_compile_definitions(airgap_qr PUBLIC AIRGAP_HAS_ZXING=1)
    target_link_libraries(airgap_qr PRIVATE ${AIRGAP_ZXING_TARGETS})
else()
    target_compile_definitions(airgap_qr PUBLIC AIRGAP_HAS_ZXING=0)
endif()

if(MSVC)
    target_compile_options(airgap_qr PRIVATE /W4 /permissive-)
else()
    target_compile_options(airgap_qr PRIVATE -Wall -Wextra -Wpedantic)
endif()
