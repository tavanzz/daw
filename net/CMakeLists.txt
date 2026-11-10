# Connections for co-op sessions: TLS 1.3 links (Mbed TLS) between the app and the server,
# frames, a reliable channel over them. Knows nothing of projects (see CLAUDE.md,
# Architecture).

add_library(daw_net STATIC
    src/FrameReader.cpp
    src/ReliableChannel.cpp
    src/Tls.cpp
    src/TlsLink.cpp
    src/TlsThreading.cpp)
add_library(daw::net ALIAS daw_net)

target_include_directories(daw_net PUBLIC include PRIVATE src)
target_compile_features(daw_net PUBLIC cxx_std_20)
target_link_libraries(daw_net PUBLIC daw::juce PRIVATE mbedtls mbedx509 tfpsacrypto)
daw_enable_warnings(daw_net)
