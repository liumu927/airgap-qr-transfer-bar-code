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

find_package(Qt6 QUIET COMPONENTS Core Gui Quick QuickControls2 Qml Multimedia)

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
    send_controller.cpp
    send_controller.hpp
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
        airgap_app_core
)

target_compile_features(airgap_qr_transfer_app PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(airgap_qr_transfer_app PRIVATE /W4 /permissive-)
else()
    target_compile_options(airgap_qr_transfer_app PRIVATE -Wall -Wextra -Wpedantic)
endif()
