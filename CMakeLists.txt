cmake_minimum_required(VERSION 3.20)
project(AnoDrop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Curses REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(LibtorrentRasterbar REQUIRED)
find_package(spdlog REQUIRED)

add_executable(AnoDrop
    main.cpp
    core/DhtPublisher.cpp
    crypto/HandShakeChannel.cpp
    network/Dropper.cpp
    ui/GuiBuilder.cpp
    network/DhtManager.cpp
    utils/Logger.cpp
    network/AlertListener.cpp
)

target_link_libraries(AnoDrop
    PRIVATE
    ${CURSES_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    LibtorrentRasterbar::torrent-rasterbar
    spdlog::spdlog
)

target_include_directories(AnoDrop PRIVATE ${CURSES_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
