add_executable(airgap_app_tests
    qr_loopback_tests.cpp
    receive_frame_collector_tests.cpp
    send_frame_builder_tests.cpp
)

target_link_libraries(airgap_app_tests PRIVATE airgap_app_core)

if(MSVC)
    target_compile_options(airgap_app_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(airgap_app_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME airgap_app_tests COMMAND airgap_app_tests)
