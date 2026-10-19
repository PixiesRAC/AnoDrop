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

    std::unique_ptr<Config> configPtr;
    try
    {
        configPtr = std::make_unique<Config>(argc, argv);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Configuration error: " << e.what() << "\n";
        return 1;
    }
    const Config &config = *configPtr;
    Logger::init(config.getLogPath(), config.getLogLevel());

    SPDLOG_INFO("[BOOT] Config: LogPath={}, LogLevel={}, StorePath={}, NetworkInterface={}, MaxConcurrentFetches={}, FetchInterval={}ms, PullMessagesOnStartup={}",
                config.getLogPath(), config.getLogLevel(), config.getStorePath(), config.getNetworkInterface(),
                config.getMaxConcurrentFetches(), config.getFetchInterval(), config.getPullMessagesOnStartup());

    Hasher::getInstance().setConfig(config);
    const std::string storePath = config.getStorePath();
    ui::GuiBuilder gui;

    std::shared_ptr<LobbyStore> lobbyStore;
    std::string errorMessage = "";

    while (true)
    {
        std::string masterPwd = ui::Unlock::askMasterPassword(errorMessage);
        if (masterPwd.empty())
        {
            return 0;
        }
        gui.showLoadingScreen();
        try
        {
            lobbyStore = std::make_shared<LobbyStore>(masterPwd, storePath);
            OPENSSL_cleanse(masterPwd.data(), masterPwd.size());
            break;
        }
        catch (const std::exception &e)
        {
            OPENSSL_cleanse(masterPwd.data(), masterPwd.size());
            errorMessage = e.what();
        }
    }

    std::unique_ptr<DhtManager> dhtManager;
    try
    {
        dhtManager = std::make_unique<DhtManager>(config.getNetworkInterface());
    }
    catch (const std::exception &e)
    {
        SPDLOG_CRITICAL("[BOOT] Failed to initialize DHT Manager: {}", e.what());
        gui.resetTerminal();
        std::cerr << "\n\033[1;31m[AnoDrop KILLSWITCH ERROR] " << e.what() << "\033[0m\n\n";
        return 1;
    }
    std::shared_ptr<DhtPublisher> dhtPublisher;

    if (dhtManager->hasSession())
    {
        dhtManager->startAlertThread();
        if (!dhtManager->waitUntilDhtIsReady()) {
            SPDLOG_WARN("[BOOT] DHT bootstrap timed out; continuing with background recovery");
        }
        dhtPublisher = std::make_shared<DhtPublisher>(lobbyStore, std::move(dhtManager), config.getMaxConcurrentFetches());
        
        auto appFacade = std::make_unique<AppFacade>(dhtPublisher, lobbyStore, config.getPullMessagesOnStartup());
        if (config.getPullMessagesOnStartup())
        {
            SPDLOG_INFO("[BOOT] PullMessagesOnStartup is enabled: Synchronizing all active lobbies.");
            appFacade->synchronizeAllActiveLobbies();
        }
        gui.setAppFacade(std::move(appFacade));
    }
    else
    {
        SPDLOG_ERROR("[BOOT] Failed to create DHT session ... exiting!");
        return 1;
    }

    std::unique_ptr<LobbyFetcher> lobbyStateFetcher = std::make_unique<LobbyFetcher>(dhtPublisher, lobbyStore, config.getFetchInterval());

    lobbyStateFetcher->ProcessLobbies();
    gui.buildAndRun();

    SPDLOG_INFO("[SHUTDOWN] Shutting down AnoDrop...");
    lobbyStateFetcher->stop();
    gui.shutdown();
    dhtPublisher->shutdown();
    SPDLOG_INFO("[SHUTDOWN] AnoDrop shutdown completed cleanly.");
    return 0;
}
