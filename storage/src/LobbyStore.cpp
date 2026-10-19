#include "LogEvents.h"
#include "LobbyStore.h"

#include <fstream>
#include <chrono>
#include <future>
#include <nlohmann/json.hpp>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <spdlog/spdlog.h>

#include "CryptoUtils.h"
#include "Hasher.h"
#include "Lobby.h"
#include "LobbyDataSerializer.h"
#include "LobbyUpdate.h"
#include "LobbyUpdateCodec.h"
#include "Util.h"

namespace
{
void syncPath(const std::filesystem::path &path, bool directory = false)
{
    const std::filesystem::path target = path.empty() ? std::filesystem::path(".") : path;
    const int fd = ::open(target.c_str(), O_RDONLY | O_CLOEXEC | (directory ? O_DIRECTORY : 0));
    if (fd < 0)
        throw std::runtime_error("Cannot open path for durable sync");
    const int result = ::fsync(fd);
    ::close(fd);
    if (result != 0)
        throw std::runtime_error("Durable sync failed");
}
} // namespace

LobbyStore::LobbyStore(const std::string &masterPassword, const std::string &storeDir) : m_storeDir(storeDir)
{
    const auto started = std::chrono::steady_clock::now();
    std::error_code ec;
    std::filesystem::create_directories(m_storeDir, ec);
    std::filesystem::permissions(m_storeDir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);

    m_journal = std::make_shared<EncryptedJournal>(std::filesystem::path(m_storeDir) / "transactions.v1.enc");

    std::filesystem::path vaultPath = std::filesystem::path(m_storeDir) / "vault.enc";

    if (std::filesystem::exists(vaultPath))
    {
        std::ifstream inFile(vaultPath, std::ios::binary);
        if (!inFile)
            throw std::runtime_error("Failed to read vault.enc");

        std::vector<unsigned char> vaultData(76); // 16 salt + 12 IV + 32 ciphertext + 16 tag
        if (!inFile.read(reinterpret_cast<char *>(vaultData.data()), static_cast<std::streamsize>(vaultData.size())))
        {
            throw std::runtime_error("Corrupted vault.enc file");
        }

        std::vector<unsigned char> salt(vaultData.begin(), vaultData.begin() + 16);
        std::vector<unsigned char> encryptedDek(vaultData.begin() + 16, vaultData.end());

        std::vector<unsigned char> kek = CryptoUtils::deriveStoreKey(masterPassword, salt);
        auto dekRes = CryptoUtils::decryptAES256GCM(encryptedDek, kek);
        OPENSSL_cleanse(kek.data(), kek.size());

        if (!dekRes)
        {
            SPDLOG_WARN("[VAULT] Unlock failed: incorrect password or corrupted vault");
            throw std::runtime_error("Wrong master password or corrupted vault");
        }
        m_masterKey->bytes = std::move(*dekRes);
        SPDLOG_INFO("[VAULT] Vault unlocked");
    }
    else
    {
        if (std::filesystem::file_size(std::filesystem::path(m_storeDir) / "transactions.v1.enc") != 0)
            throw std::runtime_error("Vault missing for non-empty journal");
        std::vector<unsigned char> salt(16);
        m_masterKey->bytes.resize(32);
        if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1 || RAND_bytes(m_masterKey->bytes.data(), static_cast<int>(m_masterKey->bytes.size())) != 1)
        {
            throw std::runtime_error("RAND_bytes failed");
        }

        std::vector<unsigned char> kek = CryptoUtils::deriveStoreKey(masterPassword, salt);
        auto encryptedDekRes = CryptoUtils::encryptAES256GCM(m_masterKey->bytes, kek);
        OPENSSL_cleanse(kek.data(), kek.size());

        if (!encryptedDekRes)
        {
            throw std::runtime_error("Failed to encrypt new DEK");
        }

        std::vector<unsigned char> vaultData;
        vaultData.reserve(salt.size() + encryptedDekRes->size());
        vaultData.insert(vaultData.end(), salt.begin(), salt.end());
        vaultData.insert(vaultData.end(), encryptedDekRes->begin(), encryptedDekRes->end());

        std::ofstream outFile(vaultPath, std::ios::binary);
        if (!outFile || !outFile.write(reinterpret_cast<const char *>(vaultData.data()), static_cast<std::streamsize>(vaultData.size())))
        {
            throw std::runtime_error("Failed to write vault.enc");
        }
        outFile.close();
        if (outFile.fail())
            throw std::runtime_error("Failed to close vault");
        syncPath(vaultPath);
        syncPath(m_storeDir, true);
        SPDLOG_INFO("[VAULT] New vault created and synchronized");
        std::filesystem::permissions(vaultPath, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace, ec);
    }

    SPDLOG_INFO("[VAULT] Vault ready elapsed_ms={}", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
    std::size_t replayed = 0;
    m_lobbyRegistry = std::make_unique<LobbyRegistry>();
    m_journal->replay(m_masterKey->bytes, [this, &replayed](std::span<const unsigned char> bytes, EncryptedJournal::Location location)
                      {
        ++replayed;
        const auto record = nlohmann::json::parse(bytes.begin(), bytes.end());
        if (record.at("version").get<int>() != 1) throw std::runtime_error("Unsupported journal version");
        auto update = record.at("update").get<LobbyUpdate>();
        getOrCreateLobby(record.at("lobby").get<std::string>())->onUpdate(update, update.new_message ? journalMessageLoader(location) : MessageLoader{}); });
    SPDLOG_INFO("[STORE] Journal replay completed transactions={} lobbies={}", replayed, m_lobbyRegistry->all().size());
}

LobbyStore::~LobbyStore()
{
    m_ioWorker.stopAndDrain();
    SPDLOG_INFO("[SHUTDOWN] Storage queue drained");
}

bool LobbyStore::execute(const std::string &lobbyName, lobby::Command command)
{
    return transact(lobbyName, [command = std::move(command)](const LobbyData &state)
                    { return lobby::prepare(state, command); });
}

bool LobbyStore::commit(const std::string &lobbyName, const LobbyUpdate &update)
{
    return transact(lobbyName, [update](const LobbyData &)
                    { return update; });
}

bool LobbyStore::transact(const std::string &lobbyName,
                          std::function<LobbyUpdate(const LobbyData &)> mutator)
{
    return runOnIoWorker([this, lobbyName, mutator = std::move(mutator)]() -> bool
                         {
        auto observer = getLobby(lobbyName);
        const bool created = !observer;
        if (created)
        {
            LobbyData initial;
            initial.metadata.lobby_name = lobbyName;
            observer = std::make_shared<Lobby>(initial);
        }
        auto update = observer->normalizeUpdate(mutator(observer->getLobbyDataWithoutHistory()));
        const bool changed = !update.empty();
        if (changed)
        {
            nlohmann::json record{{"version", 1}, {"lobby", lobbyName}, {"update", update}};
            SPDLOG_DEBUG("[STORE] Lobby update prepared field_count={}", record["update"].size());
            std::string bytes = record.dump();
            const auto writeStarted = std::chrono::steady_clock::now();
            EncryptedJournal::Location location{};
            try
            {
                location = m_journal->append(std::span<const unsigned char>(
                                                 reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size()),
                                             m_masterKey->bytes);
            }
            catch (...)
            {
                OPENSSL_cleanse(bytes.data(), bytes.size());
                m_needsRecovery = true;
                SPDLOG_CRITICAL("[STORE] Journal write failed; recovery required");
                throw StoreRecoveryRequired();
            }
            OPENSSL_cleanse(bytes.data(), bytes.size());
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - writeStarted).count();
            const auto id = LogEvents::lobbyId(lobbyName);
            SPDLOG_DEBUG("[STORE] lobby={} transaction={} write_ms={:.3f} | Journal synchronized", id, location.index, elapsed);
            if (update.new_message) {
                const auto &message = *update.new_message;
                SPDLOG_INFO("[{}] lobby={} seq={} write_ms={:.3f} | Message persisted",
                            message.direction == MessageDirection::OUT ? "MSG-OUT" : "MSG-IN", id, message.seq_num, elapsed);
            }
            if (update.peer_acked_seq_nums)
                SPDLOG_INFO("[ACK] lobby={} ack={} | Peer receipts persisted", id, nlohmann::json(*update.peer_acked_seq_nums).dump());
            if (update.status)
                SPDLOG_INFO("[LOBBY] lobby={} status={} | State persisted", id,
                            *update.status == LobbyStatus::HANDSHAKE_DONE ? "READY" :
                            *update.status == LobbyStatus::CONFIRMATION_PENDING ? "CONFIRMATION_PENDING" :
                            *update.status == LobbyStatus::ANOMALY ? "ANOMALY" : "PENDING");
            if (update.is_hidden)
                SPDLOG_INFO("[LOBBY] lobby={} hidden={} | Visibility persisted", id, *update.is_hidden);
            try
            {
                observer->onUpdate(update, update.new_message ? journalMessageLoader(location) : MessageLoader{});
                if (created)
                    m_lobbyRegistry->add(observer);
                ++m_transactionCount;
                if (m_transactionCount >= compactionThreshold())
                {
                    compactUnlocked();
                }
            }
            catch (...)
            {
                m_needsRecovery = true;
                throw StoreRecoveryRequired();
            }
        }
        return changed; });
}

void LobbyStore::compact()
{
    runOnIoWorker([this]
                  {
        try
        {
            compactUnlocked();
        }
        catch (...)
        {
            m_needsRecovery = true;
            throw;
        } });
}

void LobbyStore::compactUnlocked()
{
    const auto compactStarted = std::chrono::steady_clock::now();
    SPDLOG_INFO("[COMPACT] Compaction started transactions={}", m_transactionCount);
    const std::filesystem::path finalPath = std::filesystem::path(m_storeDir) / "transactions.v1.enc";
    const std::filesystem::path tempPath = std::filesystem::path(m_storeDir) / "transactions.v1.enc.compact.tmp";

    std::error_code ec;
    const auto bytesBefore = std::filesystem::file_size(finalPath, ec);
    if (std::filesystem::exists(tempPath, ec))
    {
        std::filesystem::remove(tempPath, ec);
    }

    struct PendingMessageLoader
    {
        std::string lobbyName;
        ChatMessage metadata;
        EncryptedJournal::Location location;
    };
    std::vector<PendingMessageLoader> pendingLoaders;

    auto observers = m_lobbyRegistry->all();

    auto tempJournal = std::make_shared<EncryptedJournal>(tempPath);
    {

        for (const auto &obs : observers)
        {
            std::string lobbyName = obs->getLobbyName();
            LobbyData state = obs->getLobbyDataWithoutHistory();

            // 1. Write each chat message first so sequence numbers correspond to stored messages
            constexpr std::size_t pageSize = 256;
            for (std::size_t offset = 0, total = obs->historySize(); offset < total; offset += pageSize)
            {
                const auto messages = obs->historyPage(offset, pageSize);
                for (const auto &msg : messages)
                {
                    LobbyUpdate msgUpdate;
                    msgUpdate.new_message = msg;
                    if (msg.direction == MessageDirection::OUT)
                    {
                        msgUpdate.my_seq_num = msg.seq_num;
                    }
                    nlohmann::json msgRecord{{"version", 1}, {"lobby", lobbyName}, {"update", msgUpdate}};
                    std::string msgBytes = msgRecord.dump();
                    auto loc = tempJournal->append(std::span<const unsigned char>(
                                                       reinterpret_cast<const unsigned char *>(msgBytes.data()), msgBytes.size()),
                                                   m_masterKey->bytes, false);
                    OPENSSL_cleanse(msgBytes.data(), msgBytes.size());

                    ChatMessage meta = msg;
                    meta.content.clear();
                    pendingLoaders.push_back({lobbyName, std::move(meta), loc});
                }
            }

            // 2. Write the consolidated state snapshot for this lobby (metadata, ACKs, heartbeats, keys)
            LobbyUpdate snapshot;
            snapshot.status = state.metadata.status;
            snapshot.my_pseudo = state.metadata.my_pseudo;
            snapshot.other_pseudo = state.metadata.other_pseudo;
            snapshot.fake_timezone = state.metadata.fake_timezone;
            snapshot.read_messages_count = state.metadata.read_messages_count;
            snapshot.is_hidden = state.metadata.is_hidden;
            snapshot.anomaly_message = state.metadata.anomaly_message;
            snapshot.handshake_error = state.metadata.handshake_error;
            snapshot.last_heartbeat = state.metadata.last_heartbeat;

            if (state.crypto)
            {
                snapshot.my_x25519_pub = state.crypto->my_x25519_pub;
                snapshot.my_x25519_priv = state.crypto->my_x25519_priv;
                snapshot.announced_x25519_pub = state.crypto->announced_x25519_pub;
                snapshot.private_node_discussion = state.crypto->private_node_discussion;
            }

            snapshot.announced_seq_num = state.sync.announced_seq_num;
            snapshot.dht_acked_seq_nums = state.sync.dht_acked_seq_nums;
            snapshot.peer_acked_seq_nums = state.sync.peer_acked_seq_nums;
            snapshot.resynced_seq_nums = state.sync.resynced_seq_nums;

            nlohmann::json snapRecord{{"version", 1}, {"lobby", lobbyName}, {"update", snapshot}};
            std::string snapBytes = snapRecord.dump();
            tempJournal->append(std::span<const unsigned char>(
                                    reinterpret_cast<const unsigned char *>(snapBytes.data()), snapBytes.size()),
                                m_masterKey->bytes, false);
            OPENSSL_cleanse(snapBytes.data(), snapBytes.size());
        }
    }

    // Keep the compacted journal open: its record counter and exclusive lock
    // must survive the rename. Reopening without replay would reset the counter.
    syncPath(tempPath);
    std::filesystem::rename(tempPath, finalPath);
    m_journal = std::move(tempJournal);
    syncPath(m_storeDir, true);

    // Group loaders by lobby and update observers
    std::map<std::string, std::vector<HistoryRecord>> newRecords;
    for (auto &item : pendingLoaders)
    {
        newRecords[item.lobbyName].push_back({std::move(item.metadata),
                                              journalMessageLoader(item.location)});
    }

    for (const auto &obs : observers)
    {
        std::string name = obs->getLobbyName();
        obs->resetHistoryRecords(std::move(newRecords[name]));
    }

    SPDLOG_INFO("[COMPACT] Journal size before_bytes={} after_bytes={}", bytesBefore, std::filesystem::file_size(finalPath, ec));
    m_transactionCount = 0;
    SPDLOG_INFO("[COMPACT] Compaction completed lobbies={} elapsed_ms={}", observers.size(), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - compactStarted).count());
}

std::size_t LobbyStore::compactionThreshold() const
{
    std::size_t totalHistory = 0;
    for (const auto &obs : m_lobbyRegistry->all())
    {
        totalHistory += obs->historySize();
    }
    return std::max<std::size_t>(1000, totalHistory);
}

std::shared_ptr<LobbyObserver> LobbyStore::getLobby(const std::string &lobbyName)
{
    return m_lobbyRegistry->find(lobbyName);
}

std::shared_ptr<LobbyObserver> LobbyStore::getOrCreateLobby(const std::string &lobbyName)
{
    return m_lobbyRegistry->getOrCreate(lobbyName, [&]()
                                        {
        SPDLOG_INFO("[STORE] Creating new lobby: {}", LogEvents::lobbyId(lobbyName));
        LobbyData lobbyData;
        lobbyData.metadata.lobby_name = lobbyName;
        return std::make_shared<Lobby>(lobbyData); });
}

std::vector<std::shared_ptr<LobbyObserver>> LobbyStore::getLobbies()
{
    return m_lobbyRegistry->all();
}

void LobbyStore::updateMasterPassword(const std::string &newPassword)
{
    try
    {
        runOnIoWorker([this, pwd = SecureString(newPassword)]()
                      {
            std::filesystem::path vaultPath = std::filesystem::path(m_storeDir) / "vault.enc";
            std::filesystem::path tmpVaultPath = vaultPath.string() + ".tmp";

            std::vector<unsigned char> newSalt(16);
            if (RAND_bytes(newSalt.data(), static_cast<int>(newSalt.size())) != 1)
            {
                throw std::runtime_error("RAND_bytes failed");
            }

            std::vector<unsigned char> newKek = CryptoUtils::deriveStoreKey(pwd, newSalt);

            const auto &currentDek = m_masterKey->bytes;

            auto encryptedDekRes = CryptoUtils::encryptAES256GCM(currentDek, newKek);
            OPENSSL_cleanse(newKek.data(), newKek.size());

            if (!encryptedDekRes)
            {
                throw std::runtime_error("Failed to encrypt DEK with new password");
            }

            std::vector<unsigned char> vaultData;
            vaultData.reserve(newSalt.size() + encryptedDekRes->size());
            vaultData.insert(vaultData.end(), newSalt.begin(), newSalt.end());
            vaultData.insert(vaultData.end(), encryptedDekRes->begin(), encryptedDekRes->end());

            std::ofstream outFile(tmpVaultPath, std::ios::binary);
            if (!outFile || !outFile.write(reinterpret_cast<const char *>(vaultData.data()), static_cast<std::streamsize>(vaultData.size())))
            {
                if (std::filesystem::exists(tmpVaultPath))
                {
                    std::filesystem::remove(tmpVaultPath);
                }
                throw std::runtime_error("Failed to write new vault.tmp");
            }
            outFile.close();
            if (outFile.fail())
            {
                if (std::filesystem::exists(tmpVaultPath))
                {
                    std::filesystem::remove(tmpVaultPath);
                }
                throw std::runtime_error("Failed to close new vault.tmp");
            }

            std::error_code permEc;
            std::filesystem::permissions(tmpVaultPath, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace, permEc);

            // Atomic rename guarantees the old vault is overwritten safely
            syncPath(tmpVaultPath);
            std::filesystem::rename(tmpVaultPath, vaultPath);
            syncPath(m_storeDir, true);

            SPDLOG_INFO("[VAULT] Password rotation synchronized"); });
    }
    catch (const std::exception &e)
    {
        SPDLOG_ERROR("[VAULT] Failed to update master password: {}", e.what());
        throw; // Rethrow to propagate to the UI
    }
}

MessageLoader LobbyStore::journalMessageLoader(EncryptedJournal::Location location) const
{
    return [journal = m_journal, key = m_masterKey, location]
    {
        auto plain = journal->read(location, key->bytes);
        ScopedCleanse plainGuard(plain);
        return nlohmann::json::parse(plain.begin(), plain.end()).at("update").at("new_message").get<ChatMessage>();
    };
}

std::expected<HandshakeTransitionResult, std::string> LobbyStore::advanceHandshake(
    const std::string &lobbyName, const std::string &ownPayload,
    std::int64_t sequence, const std::string &value, bool found)
{
    std::expected<HandshakeTransitionResult, std::string> result{HandshakeTransitionResult{}};
    transact(lobbyName, [&](const LobbyData &state)
             {
        if (state.metadata.is_hidden ||
            (state.metadata.status != LobbyStatus::PENDING_HANDSHAKE &&
             state.metadata.status != LobbyStatus::CONFIRMATION_PENDING)) return LobbyUpdate{};
        result = HandshakeStateEngine::processHandshakeTransition(ownPayload, state, sequence, value, found);
        if (!result) return lobby::prepare(state, lobby::RecordHandshakeError{result.error()});
        result->update.handshake_error = "";
        return result->update; });
    return result;
}
