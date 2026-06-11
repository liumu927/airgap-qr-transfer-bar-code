add_library(airgap_app_core
    cimbar_payload.cpp
    cimbar_payload.hpp
    receive_frame_collector.cpp
    receive_frame_collector.hpp
    send_frame_builder.cpp
    send_frame_builder.hpp
    transfer_speed.hpp
)

target_include_directories(airgap_app_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(airgap_app_core PUBLIC airgap_core airgap_qr)

target_compile_features(airgap_app_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(airgap_app_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(airgap_app_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(Qt6 QUIET COMPONENTS Core Gui Quick QuickControls2 Qml Multimedia SerialPort)

if(NOT Qt6_FOUND)
    message(STATUS "Qt6 was not found; skipping AirGap QR Transfer Qt app target.")
    return()
endif()

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(airgap_qr_transfer_app
    main.cpp
    qr_image_provider.cpp
    qr_image_provider.hpp
    receive_controller.cpp
    receive_controller.hpp
    scanner_receive_controller.cpp
    scanner_receive_controller.hpp
    send_controller.cpp
    send_controller.hpp
    serial_port_reader.cpp
    serial_port_reader.hpp
    transfer_speed.hpp
    video_frame_luminance.cpp
    video_frame_luminance.hpp
)

if(ANDROID)
    set_target_properties(airgap_qr_transfer_app PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
        QT_ANDROID_SDK_BUILD_TOOLS_REVISION "33.0.2"
        QT_ANDROID_MIN_SDK_VERSION "26"
        QT_ANDROID_TARGET_SDK_VERSION "33"
    )
    target_link_libraries(airgap_qr_transfer_app PRIVATE Qt6::CorePrivate)
endif()

qt_add_qml_module(airgap_qr_transfer_app
    URI AirGapQrTransfer
    VERSION 1.0
    QML_FILES
        Main.qml
)

target_link_libraries(airgap_qr_transfer_app
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Qml
        Qt6::Multimedia
        Qt6::SerialPort
        airgap_app_core
)

target_compile_features(airgap_qr_transfer_app PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(airgap_qr_transfer_app PRIVATE /W4 /permissive-)
else()
    target_compile_options(airgap_qr_transfer_app PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Auto-deploy Qt DLLs alongside the executable after each build.
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")
if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET airgap_qr_transfer_app POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}"
            "$<TARGET_FILE:airgap_qr_transfer_app>"
        COMMENT "Deploying Qt runtime alongside airgap_qr_transfer_app..."
    )
endif()

# Copy vcpkg runtime DLLs to the output directory.
set(_vcpkg_bin_dir "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/bin")
if(EXISTS "${_vcpkg_bin_dir}")
    file(GLOB _vcpkg_runtime_dlls "${_vcpkg_bin_dir}/*.dll")
    if(_vcpkg_runtime_dlls)
        add_custom_command(TARGET airgap_qr_transfer_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_vcpkg_runtime_dlls}
                "$<TARGET_FILE_DIR:airgap_qr_transfer_app>"
            COMMENT "Copying vcpkg runtime DLLs..."
        )
    endif()
endif()
