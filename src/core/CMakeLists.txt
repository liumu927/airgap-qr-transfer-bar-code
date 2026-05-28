add_library(airgap_core
    assembler.cpp
    chunker.cpp
    crc32.cpp
    frame.cpp
    session.cpp
    sha256.cpp
)

target_include_directories(airgap_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(airgap_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(airgap_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(airgap_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
