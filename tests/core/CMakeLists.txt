add_executable(airgap_core_tests
    core_flow_tests.cpp
    frame_codec_tests.cpp
)

target_link_libraries(airgap_core_tests PRIVATE airgap_core)

if(MSVC)
    target_compile_options(airgap_core_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(airgap_core_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME airgap_core_tests COMMAND airgap_core_tests)
