cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

project(AnoDrop VERSION 0.1.0 LANGUAGES CXX)

# Default to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build.")
endif()
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wnon-virtual-dtor
)

find_package(Curses REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(LibtorrentRasterbar REQUIRED)
find_package(spdlog REQUIRED)

find_package(nlohmann_json REQUIRED)

add_executable(AnoDrop
    main.cpp
    core/DhtPublisher.cpp
    core/Config.cpp
    lobby/LobbyStore.cpp
    lobby/LobbyListener.cpp
    lobby/Lobby.cpp
    lobby/LobbyFetcher.cpp
    crypto/CryptoUtils.cpp
    network/Dropper.cpp
    ui/GuiBuilder.cpp
    network/DhtManager.cpp
    utils/Logger.cpp
    utils/WorkerThread.cpp
    network/AlertListener.cpp
    ui/Unlock.cpp
    crypto/HandshakeKeyService.cpp
    crypto/HandshakePayloadCodec.cpp
    lobby/HandshakeStateEngine.cpp
    ui/HandshakeView.cpp
    ui/ChangePasswordView.cpp
    ui/LobbiesView.cpp
    ui/ChatView.cpp
    ui/GuiPreferences.cpp
    ui/AnomalyFlagView.cpp
    core/AppFacade.cpp
    chat/DhtChat.cpp
    chat/DhtChatHandler.cpp
    chat/ChatResyncer.cpp
)

target_link_libraries(AnoDrop
    PRIVATE
    ${CURSES_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    LibtorrentRasterbar::torrent-rasterbar
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    argon2
)

target_include_directories(AnoDrop PRIVATE ${CURSES_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(decrypt_store
    tools/decrypt_store.cpp
    lobby/LobbyStore.cpp
    lobby/LobbyListener.cpp
    lobby/Lobby.cpp
    crypto/CryptoUtils.cpp
    utils/WorkerThread.cpp
)

target_link_libraries(decrypt_store
    PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

target_include_directories(decrypt_store PRIVATE ${OPENSSL_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(test_crypto
    tests/test_crypto.cpp
    crypto/CryptoUtils.cpp
    crypto/HandshakeKeyService.cpp
    crypto/HandshakePayloadCodec.cpp
    lobby/HandshakeStateEngine.cpp
    network/DhtManager.cpp
    network/AlertListener.cpp
    lobby/LobbyStore.cpp
    lobby/LobbyListener.cpp
    lobby/Lobby.cpp
    core/Config.cpp
    utils/WorkerThread.cpp
)

target_link_libraries(test_crypto
    PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    LibtorrentRasterbar::torrent-rasterbar
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    argon2
    gtest_main
)

target_include_directories(test_crypto PRIVATE ${OPENSSL_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

include(GoogleTest)
gtest_discover_tests(test_crypto)
