add_executable(airgap_qr_tests
    qr_adapter_tests.cpp
)

target_link_libraries(airgap_qr_tests PRIVATE airgap_qr)

if(MSVC)
    target_compile_options(airgap_qr_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(airgap_qr_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME airgap_qr_tests COMMAND airgap_qr_tests)
