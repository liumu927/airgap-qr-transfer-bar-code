add_library(airgap_qr
    qr_adapter.cpp
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
