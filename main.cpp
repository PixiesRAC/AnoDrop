#include "core/DhtPublisher.h"
#include "crypto/Hasher.h"
#include "lobby/LobbyFetcher.h"
#include "lobby/LobbyStore.h"
#include "network/DhtManager.h"
#include "ui/GuiBuilder.h"
#include "ui/Unlock.h"
#include "utils/Logger.h"

#include <memory>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

#include "core/Config.h"

#include <iostream>

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help")
        {
            std::cout << "AnoDrop - Decentralized messaging app\n\n"
                      << "Usage: " << argv[0] << " [options]\n\n"
                      << "Options:\n"
                      << "  -h, --help       Show this help message and exit\n"
                      << "  -c, --config     Path to a JSON configuration file\n\n"
                      << "Example:\n"
                      << "  " << argv[0] << " -c config.json\n";
            return 0;
        }
    }

    Config config(argc, argv);
    Logger::init(config.getLogPath());
    Hasher::getInstance().setConfig(config);
    const std::string storePath = config.getStorePath();

    ui::GuiBuilder gui;
    const std::string masterPwd = ui::Unlock::askMasterPassword();
    gui.showLoadingScreen();

    std::shared_ptr<LobbyStore> lobbyStore = std::make_shared<LobbyStore>(masterPwd, storePath);
    std::unique_ptr<DhtManager> dhtManager = std::make_unique<DhtManager>();
    std::shared_ptr<DhtPublisher> dhtPublisher = std::make_shared<DhtPublisher>();

    if (dhtManager->isSessionReady())
    {
        dhtManager->startAlertThread();
        dhtManager->waitUntilDhtIsReady();
        dhtPublisher->setDhtManager(std::move(dhtManager));
        dhtPublisher->setLobbyStore(lobbyStore);
        gui.setDhtPublisher(dhtPublisher);
        gui.setLobbyStore(lobbyStore);
        gui.setAppFacade(std::make_unique<AppFacade>(dhtPublisher, lobbyStore));
    }
    else
    {
        SPDLOG_ERROR("Failed to create DHT session ... exiting!");
        return 1;
    }

    std::unique_ptr<LobbyFetcher> lobbyFetcher = std::make_unique<LobbyFetcher>(dhtPublisher, lobbyStore, config.getFetchInterval());

    lobbyFetcher->ProcessLobbies();
    gui.buildAndRun(); // loop

    SPDLOG_INFO("Shutting down AnoDrop...");
    lobbyFetcher->stop();
    SPDLOG_INFO("LobbyFetcher stopped. Cleaning up...");
    return 0;
}
