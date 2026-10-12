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
add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE FMT_CONSTEVAL=constexpr)

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
    network/src/DhtPublisher.cpp
    network/src/NetworkResyncHandler.cpp
    network/src/DhtSignalingService.cpp
    anomaly/src/AnomalyHandler.cpp
    heartbeat/src/HeartBeatHandler.cpp
    ack/src/AckHandler.cpp
    core/src/Config.cpp
    storage/src/LobbyStore.cpp
    lobby/src/LobbyListener.cpp
    lobby/src/LobbyDataSerializer.cpp
    lobby/src/Lobby.cpp
    lobby/src/LobbyFetcher.cpp
    crypto/src/CryptoUtils.cpp
    network/src/Dropper.cpp
    ui/src/GuiBuilder.cpp
    network/src/DhtManager.cpp
    network/src/KillSwitch.cpp
    utils/src/Logger.cpp
    utils/src/IoWorker.cpp
    utils/src/PartitionedThreadPool.cpp
    utils/src/TimeUtils.cpp
    utils/src/NetworkUtils.cpp
    network/src/AlertListener.cpp
    ui/src/Unlock.cpp
    crypto/src/HandshakeKeyService.cpp
    crypto/src/HandshakePayloadCodec.cpp
    lobby/src/HandshakeStateEngine.cpp
    ui/src/HandshakeView.cpp
    ui/src/ChangePasswordView.cpp
    ui/src/LobbiesView.cpp
    ui/src/ChatView.cpp
    ui/src/ChatViewModel.cpp
    ui/src/MessageStatusResolver.cpp
    ui/src/AnomalyFlagView.cpp
    core/src/AppFacade.cpp
    chat/src/DhtChat.cpp
    chat/src/DhtChatHandler.cpp
    chat/src/ChatResyncer.cpp
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

target_include_directories(AnoDrop PRIVATE ${CURSES_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/anomaly/include ${CMAKE_CURRENT_SOURCE_DIR}/ack/include ${CMAKE_CURRENT_SOURCE_DIR}/chat/include ${CMAKE_CURRENT_SOURCE_DIR}/core/include ${CMAKE_CURRENT_SOURCE_DIR}/crypto/include ${CMAKE_CURRENT_SOURCE_DIR}/heartbeat/include ${CMAKE_CURRENT_SOURCE_DIR}/lobby/include ${CMAKE_CURRENT_SOURCE_DIR}/network/include ${CMAKE_CURRENT_SOURCE_DIR}/storage/include ${CMAKE_CURRENT_SOURCE_DIR}/ui/include ${CMAKE_CURRENT_SOURCE_DIR}/utils/include)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests/regression)
endif()
