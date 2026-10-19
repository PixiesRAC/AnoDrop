#include <iostream>
#include <stdexcept>
#include <thread>
#include <sstream>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>
#include "DhtLog.h"

#include "DhtManager.h"

void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
class DhtManagerTestAccess
{
  public:
    static void run()
    {
        DhtManager manager(nullptr);
        // Check result context/levels without opening a network session.
        std::ostringstream captured;
        const auto previousLogger = spdlog::default_logger();
        auto logger = std::make_shared<spdlog::logger>("dht-log-test", std::make_shared<spdlog::sinks::ostream_sink_mt>(captured));
        logger->set_pattern("[%l] %v");
        logger->set_level(spdlog::level::trace);
        spdlog::set_default_logger(logger);
        std::array<char, 32> publicKey{};
        const auto operation = manager.operationKey(publicKey, "salt");
        int results = 0;
        bool callbackContextValid = true;
        auto completed = [&](DhtFetchResult result) {
            ++results;
            callbackContextValid = callbackContextValid && result.requestId == 99 && result.callbackCount == 2
                && result.callbackIndex == static_cast<std::size_t>(results);
        };
        manager.m_fetchCallbacks.emplace(operation, DhtManager::FetchContext{99,
            {completed, completed}, "Example Lobby", {DhtFetchKind::Signaling, -1, "ack"}});
        manager.handleMutableItemResult(publicKey, "salt", 7, false, lt::entry("2"));
        require(results == 0 && manager.m_fetchCallbacks.contains(operation), "Intermediate alert consumed request");
        manager.handleMutableItemResult(publicKey, "salt", 7, true, lt::entry("2"));
        require(results == 2, "Final alert did not complete both waiting callbacks");
        const bool bothCallbacksCompleted = results == 2;
        bool missingResult = false;
        manager.m_fetchCallbacks.emplace(operation, DhtManager::FetchContext{100,
            {[&](DhtFetchResult result) { missingResult = result.status == DhtFetchStatus::Missing; }},
            "Example Lobby", {DhtFetchKind::Signaling, -1, "ack"}});
        manager.handleMutableItemResult(publicKey, "salt", 0, true, lt::entry());
        spdlog::set_default_logger(previousLogger);
        require(bothCallbacksCompleted && callbackContextValid, "Shared lookup callback diagnostics lost");
        require(missingResult, "Missing result changed semantics");
        const auto logged = captured.str();
        require(logged.find("[debug] [DHT] Lookup result: ack | lobby=\"Example Lobby\"") != std::string::npos, "Missing intermediate result context or debug level");
        require(logged.find("[info] [DHT] Lookup result: ack | lobby=\"Example Lobby\"") != std::string::npos, "Missing final result context or info level");
        require(logged.find("target_sha1=" + network::logging::target(publicKey, "salt")) != std::string::npos, "Missing target SHA-1");
        require(logged.find("request_id=99 found=true dht_seq=7 authoritative=true callbacks=2") != std::string::npos,
                "Found result or callback count missing from logs");
        require(logged.find("request_id=100 found=false dht_seq=0 authoritative=true callbacks=1") != std::string::npos,
                "Missing result presented as a received ACK");
        require(logged.find("value_sha1") == std::string::npos, "Unexpected payload hash");
        int cancelled = 0;
        const auto get = ++manager.m_nextGetReservation;
        auto callback = [&](DhtFetchResult result)
        {
            require(result.status == DhtFetchStatus::Cancelled, "Incorrect rollback result");
            // Reenter the same registry: callback must run after releasing the mutex.
            std::lock_guard lock(manager.m_registryMutex);
            require(!manager.m_fetchCallbacks.contains("get"), "Callback ran before removal");
            ++cancelled;
        };
        manager.m_fetchCallbacks.emplace("get", DhtManager::FetchContext{get, {callback, callback}, "lobby", {}});
        manager.cancelPendingGet("get", get + 1);
        require(cancelled == 0, "Stale GET reservation removed current operation");
        manager.cancelPendingGet("get", get);
        require(cancelled == 2 && manager.m_fetchCallbacks.empty(), "Coalesced GET callbacks lost");
        manager.cancelPendingGet("get", get);
        require(cancelled == 2, "GET completed twice");

        int putCancelled = 0;
        const auto put = manager.registerPendingPut("put", "lobby", 1, [&](bool success)
                                                    {
            require(!success, "Rollback acknowledged a PUT");
            ++putCancelled;
            require(manager.registerPendingPut("put", "lobby", 2).has_value(), "PUT callback ran under lock or before removal"); });
        require(put.has_value(), "PUT reservation rejected");
        manager.cancelPendingPut("put", *put + 1);
        require(putCancelled == 0, "Stale PUT reservation removed current operation");
        manager.cancelPendingPut("put", *put);
        manager.cancelPendingPut("put", *put);
        require(putCancelled == 1 && manager.m_pendingPuts.at("put").sequence == 2,
                "Old rollback removed the callback's new publication");

        bool captureDestroyed = false;
        auto capture = std::shared_ptr<int>(new int(0), [&](int *value)
                                            {
            delete value;
            manager.setNetworkRecoveredCallback({});
            captureDestroyed = true; });
        manager.setPutFailureCallback([capture](const std::string &, std::int64_t) {});
        capture.reset();
        manager.setPutFailureCallback({});
        require(captureDestroyed, "Replaced callback capture was retained");

        bool wrongThreadRejected = false;
        std::jthread caller([&]
                            {
            try { manager.shutdown(); }
            catch (const std::logic_error&) { wrongThreadRejected = true; }
            manager.requestStop(); });
        caller.join();
        require(wrongThreadRejected, "Joining from a non-owner thread accepted");
        require(!manager.registerPendingPut("stopped", "lobby", 3), "Stopped manager accepted work");
        manager.shutdown();
    }
};
int main()
{
    try
    {
        DhtManagerTestAccess::run();
        std::cout << "PASS registry rollback\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
