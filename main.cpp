#include "LobbyDataSerializer.h"
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>

#include "Config.h"
#include "DhtManager.h"
#include "DhtPublisher.h"
#include "GuiBuilder.h"
#include "Hasher.h"
#include "LobbyFetcher.h"
#include "LobbyStore.h"
#include "Logger.h"
#include "Unlock.h"

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
    Logger::init(config.getLogPath(), config.getLogLevel());
    
    // Log all startup parameters
    SPDLOG_INFO("Config: LogPath={}, LogLevel={}, StorePath={}, MaxConcurrentFetches={}, FetchInterval={}ms, PullMessagesOnStartup={}", 
                config.getLogPath(), config.getLogLevel(), config.getStorePath(), 
                config.getMaxConcurrentFetches(), config.getFetchInterval(), config.getPullMessagesOnStartup());

    Hasher::getInstance().setConfig(config);
    const std::string storePath = config.getStorePath();
    ui::GuiBuilder gui;

    std::shared_ptr<LobbyStore> lobbyStore;
    std::string errorMessage = "";

    while (true)
    {
        const std::string masterPwd = ui::Unlock::askMasterPassword(errorMessage);
        if (masterPwd.empty())
        {
            return 0;
        }
        gui.showLoadingScreen();
        try
        {
            lobbyStore = std::make_shared<LobbyStore>(masterPwd, storePath);
            break;
        }
        catch (const std::exception &e)
        {
            errorMessage = e.what();
        }
    }

    std::unique_ptr<DhtManager> dhtManager = std::make_unique<DhtManager>();
    dhtManager->setLobbyStore(lobbyStore);
    std::shared_ptr<DhtPublisher> dhtPublisher = std::make_shared<DhtPublisher>();
    dhtPublisher->setMaxConcurrentFetches(config.getMaxConcurrentFetches());

    if (dhtManager->isSessionReady())
    {
        dhtManager->startAlertThread();
        dhtManager->waitUntilDhtIsReady();
        dhtPublisher->setDhtManager(std::move(dhtManager));
        
        auto appFacade = std::make_unique<AppFacade>(dhtPublisher, lobbyStore);
        if (config.getPullMessagesOnStartup())
        {
            // Pull messages on startup ensures that any missed messages while offline are retrieved
            SPDLOG_INFO("PullMessagesOnStartup is enabled: Synchronizing all active lobbies.");
            appFacade->synchronizeAllActiveLobbies();
        }
        gui.setAppFacade(std::move(appFacade));
    }
    else
    {
        SPDLOG_ERROR("Failed to create DHT session ... exiting!");
        return 1;
    }

    std::unique_ptr<LobbyFetcher> lobbyStateFetcher = std::make_unique<LobbyFetcher>(dhtPublisher, lobbyStore, config.getFetchInterval());

    lobbyStateFetcher->ProcessLobbies();
    gui.buildAndRun(); // loop

    SPDLOG_INFO("Shutting down AnoDrop...");
    lobbyStateFetcher->stop();
    dhtPublisher->shutdown(); // Stops DHT and all workers securely
    SPDLOG_INFO("LobbyFetcher stopped. Cleaning up...");
    return 0;
}
