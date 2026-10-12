#include <nlohmann/json.hpp>
#include "LobbyDataSerializer.h"
#include "LobbyStore.h"

#include <fstream>
#include <future>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <spdlog/spdlog.h>

#include "CryptoUtils.h"
#include "Hasher.h"
#include "Lobby.h"
#include "Util.h"

LobbyStore::LobbyStore(const std::string &masterPassword, const std::string &storeDir) : m_storeDir(storeDir)
{
    std::error_code ec;
    std::filesystem::create_directories(m_storeDir, ec);
    std::filesystem::permissions(m_storeDir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);
    std::filesystem::create_directories(std::filesystem::path(m_storeDir) / "lobbies", ec);
    std::filesystem::permissions(std::filesystem::path(m_storeDir) / "lobbies", std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);

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
            throw std::runtime_error("Wrong master password or corrupted vault");
        }
        m_masterKey = *dekRes;
    }
    else
    {
        if (std::filesystem::exists(std::filesystem::path(m_storeDir) / "vault_salt.bin"))
        {
            throw std::runtime_error("Unsupported old vault format detected. Migration is required but not supported in this version.");
        }
        std::vector<unsigned char> salt(16);
        m_masterKey.resize(32);
        if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1 || RAND_bytes(m_masterKey.data(), static_cast<int>(m_masterKey.size())) != 1)
        {
            throw std::runtime_error("RAND_bytes failed");
        }

        std::vector<unsigned char> kek = CryptoUtils::deriveStoreKey(masterPassword, salt);
        auto encryptedDekRes = CryptoUtils::encryptAES256GCM(m_masterKey, kek);
        OPENSSL_cleanse(kek.data(), kek.size());

        if (!encryptedDekRes)
        {
            throw std::runtime_error("Failed to encrypt new DEK");
        }

        std::vector<unsigned char> vaultData;
        vaultData.insert(vaultData.end(), salt.begin(), salt.end());
        vaultData.insert(vaultData.end(), encryptedDekRes->begin(), encryptedDekRes->end());

        std::ofstream outFile(vaultPath, std::ios::binary);
        if (!outFile || !outFile.write(reinterpret_cast<const char *>(vaultData.data()), vaultData.size()))
        {
            throw std::runtime_error("Failed to write vault.enc");
        }
        outFile.close();
        std::filesystem::permissions(vaultPath, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace, ec);
    }

    m_lobbyListener = std::make_unique<LobbyListener>();
    populateLobbies();
}

LobbyStore::~LobbyStore()
{
    m_ioWorker.stopAndDrain();
    if (!m_masterKey.empty())
    {
        OPENSSL_cleanse(m_masterKey.data(), m_masterKey.capacity());
    }
}

std::expected<void, StoreError> LobbyStore::saveLobbyData(const LobbyData &data)
{
    std::string filename = Util::toHex(Hasher::getInstance().getHashMsg256(data.lobby_name)) + ".enc";
    std::filesystem::path filePath = std::filesystem::path(m_storeDir) / "lobbies" / filename;
    std::filesystem::path tmpPath = filePath.string() + ".tmp";

    nlohmann::json dataToSave = data;
    dataToSave.erase("history"); // Exclude history from the main file
    std::string jsonStr = dataToSave.dump();
    std::vector<unsigned char> plaintext(jsonStr.begin(), jsonStr.end());

    try
    {

        std::string aadStr = "v1|" + filename;
        std::vector<unsigned char> aad(aadStr.begin(), aadStr.end());

        auto ciphertextRes = CryptoUtils::encryptAES256GCM(plaintext, m_masterKey, aad);
        if (!ciphertextRes)
        {
            return std::unexpected(StoreError::EncryptionFailed);
        }
        std::vector<unsigned char> ciphertext = *ciphertextRes;

        std::ofstream outFile(tmpPath, std::ios::binary);
        if (!outFile)
        {
            SPDLOG_ERROR("saveLobbyData: Failed to open {} for writing. Errno: {} ({})", tmpPath.string(), errno, strerror(errno));
            return std::unexpected(StoreError::FileOpenFailed);
        }
        if (!outFile.write(reinterpret_cast<const char *>(ciphertext.data()), ciphertext.size()))
        {
            return std::unexpected(StoreError::WriteFailed);
        }
        outFile.close(); // Important to close before renaming

        std::error_code permEc;
        std::filesystem::permissions(tmpPath, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace, permEc);

        // Atomic rename by the OS
        std::filesystem::rename(tmpPath, filePath);

        SPDLOG_INFO("Lobby data saved successfully for {}", data.lobby_name);
        return {};
    }
    catch (const std::exception &e)
    {
        SPDLOG_ERROR("Error saving lobby data: {}", e.what());
        if (std::filesystem::exists(tmpPath))
        {
            std::filesystem::remove(tmpPath); // Cleanup in case of error
        }
        return std::unexpected(StoreError::Unknown);
    }
}

std::expected<void, StoreError> LobbyStore::appendHistory(const std::string &lobbyName, const std::vector<ChatMessage> &newMessages)
{
    if (newMessages.empty())
        return {};

    std::string filename = Util::toHex(Hasher::getInstance().getHashMsg256(lobbyName)) + ".history.enc";
    std::filesystem::path filePath = std::filesystem::path(m_storeDir) / "lobbies" / filename;

    std::ofstream outFile(filePath, std::ios::binary | std::ios::app);
    if (!outFile)
    {
        return std::unexpected(StoreError::FileOpenFailed);
    }
    std::error_code permEc;
    std::filesystem::permissions(filePath, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace, permEc);

    try
    {
        for (const auto &msg : newMessages)
        {
            nlohmann::json j = msg;
            std::string jsonStr = j.dump();

            // Obfuscate message length: pad with spaces to a multiple of 256 bytes
            size_t blockSize = 256;
            size_t padding = blockSize - (jsonStr.size() % blockSize);
            if (padding != blockSize)
            {
                jsonStr.append(padding, ' ');
            }

            std::vector<unsigned char> plaintext(jsonStr.begin(), jsonStr.end());

            std::string aadStr = "v1|" + filename + "|" + std::to_string(msg.seq_num);
            std::vector<unsigned char> aad(aadStr.begin(), aadStr.end());

            auto ciphertextRes = CryptoUtils::encryptAES256GCM(plaintext, m_masterKey, aad);
            if (!ciphertextRes)
            {
                return std::unexpected(StoreError::EncryptionFailed);
            }
            const auto &ciphertext = *ciphertextRes;

            uint32_t size = static_cast<uint32_t>(ciphertext.size());
            uint64_t seqNum = msg.seq_num;
            if (!outFile.write(reinterpret_cast<const char *>(&size), sizeof(size)) ||
                !outFile.write(reinterpret_cast<const char *>(&seqNum), sizeof(seqNum)) ||
                !outFile.write(reinterpret_cast<const char *>(ciphertext.data()), ciphertext.size()))
            {
                return std::unexpected(StoreError::WriteFailed);
            }
        }
        return {};
    }
    catch (const std::exception &e)
    {
        return std::unexpected(StoreError::Unknown);
    }
}

void LobbyStore::asyncUpdateLobbyData(const std::string &lobbyName, const nlohmann::json &jsonPatch)
{
    // Make a mutable copy of the patch so onUpdate can remove redundant data (like duplicate messages)
    nlohmann::json mutablePatch = jsonPatch;
    
    // Apply patch immediately in-memory
    auto observer = getOrCreateLobby(lobbyName);
    observer->onUpdate(mutablePatch);

    // Persist to disk asynchronously only if the patch is still meaningful
    if (!mutablePatch.empty()) {
        // Note: No global mutex is needed in updateLobbyData because all tasks for this
        // ioWorker are executed sequentially on a single dedicated thread. This guarantees consistency.
        m_ioWorker.enqueue([this, lobbyName, observer, mutablePatch]()
                           { this->updateLobbyData(lobbyName, observer, mutablePatch); });
    }
}

void LobbyStore::updateLobbyData(const std::string &lobbyName, std::shared_ptr<LobbyObserver> observer, const nlohmann::json &jsonPatch)
{
    nlohmann::json logPatch = jsonPatch;
    if (logPatch.contains("history"))
    {
        logPatch["history"] = "[history updated]";
    }
    SPDLOG_DEBUG("updateLobbyData for lobby {}: {}", lobbyName, logPatch.dump());

    bool metadataChanged = false;
    bool hasNewMessage = false;
    for (auto it = jsonPatch.begin(); it != jsonPatch.end(); ++it)
    {
        if (it.key() == "new_message")
        {
            auto msg = it.value().get<ChatMessage>();
            if (msg.direction == MessageDirection::IN)
            {
                SPDLOG_DEBUG("📥 [RECEIVED] I/O Worker: Writing incoming message to disk: '{}'", msg.content);
            }
            else
            {
                SPDLOG_DEBUG("📤 [SENT] I/O Worker: Writing outgoing message to disk: '{}'", msg.content);
            }

            auto res = appendHistory(lobbyName, {msg});
            if (!res)
                SPDLOG_ERROR("Failed to append history: {}", storeErrorToString(res.error()));
            else
                hasNewMessage = true;
        }
        else
        {
            metadataChanged = true;
        }
    }

    if (metadataChanged || hasNewMessage)
    {
        auto data = observer->getLobbyDataWithoutHistory();
        auto res = saveLobbyData(data);
        if (!res)
            SPDLOG_ERROR("Failed to save lobby metadata: {}", storeErrorToString(res.error()));
    }
}

std::expected<LobbyData, StoreError> LobbyStore::loadLobbyDataFromFile(const std::filesystem::path &filePath)
{
    if (!std::filesystem::exists(filePath))
    {
        return std::unexpected(StoreError::FileNotFound);
    }

    try
    {
        std::ifstream inFile(filePath, std::ios::binary | std::ios::ate);
        if (!inFile)
        {
            return std::unexpected(StoreError::FileOpenFailed);
        }
        std::streamsize size = inFile.tellg();
        inFile.seekg(0, std::ios::beg);

        std::vector<unsigned char> ciphertextBlob(size);
        if (!inFile.read(reinterpret_cast<char *>(ciphertextBlob.data()), size))
        {
            return std::unexpected(StoreError::ReadFailed);
        }

        std::vector<unsigned char> currentKey = m_masterKey;

        std::string filename = filePath.filename().string();
        std::string aadStr = "v1|" + filename;
        std::vector<unsigned char> aad(aadStr.begin(), aadStr.end());

        auto plaintextRes = CryptoUtils::decryptAES256GCM(ciphertextBlob, currentKey, aad);
        if (!plaintextRes)
        {
            return std::unexpected(StoreError::DecryptionFailed);
        }
        std::vector<unsigned char> plaintext = *plaintextRes;

        std::string jsonStr(plaintext.begin(), plaintext.end());
        nlohmann::json j = nlohmann::json::parse(jsonStr);
        LobbyData lobbyData = j.get<LobbyData>();

        std::string historyFilename = Util::toHex(Hasher::getInstance().getHashMsg256(lobbyData.lobby_name)) + ".history.enc";
        std::filesystem::path historyFilePath = std::filesystem::path(m_storeDir) / "lobbies" / historyFilename;

        if (std::filesystem::exists(historyFilePath))
        {
            std::ifstream histFile(historyFilePath, std::ios::binary);
            if (histFile)
            {
                while (histFile.peek() != EOF)
                {
                    uint32_t msgSize = 0;
                    if (!histFile.read(reinterpret_cast<char *>(&msgSize), sizeof(msgSize)))
                        break;

                    if (msgSize == 0 || msgSize > 10 * 1024 * 1024)
                    {
                        SPDLOG_ERROR("Corrupted history file: invalid msgSize {}", msgSize);
                        break;
                    }

                    uint64_t seqNum = 0;
                    if (!histFile.read(reinterpret_cast<char *>(&seqNum), sizeof(seqNum)))
                        break;

                    std::vector<unsigned char> msgCiphertext(msgSize);
                    if (!histFile.read(reinterpret_cast<char *>(msgCiphertext.data()), msgSize))
                        break;

                    std::string aadStrHist = "v1|" + historyFilename + "|" + std::to_string(seqNum);
                    std::vector<unsigned char> aadHist(aadStrHist.begin(), aadStrHist.end());

                    auto msgPlaintextRes = CryptoUtils::decryptAES256GCM(msgCiphertext, currentKey, aadHist);
                    if (msgPlaintextRes)
                    {
                        std::string msgJsonStr(msgPlaintextRes->begin(), msgPlaintextRes->end());
                        try
                        {
                            nlohmann::json msgJson = nlohmann::json::parse(msgJsonStr);
                            auto parsedMsg = msgJson.get<ChatMessage>();
                            lobbyData.history.messages.push_back(parsedMsg);
                            if (parsedMsg.direction == MessageDirection::IN && parsedMsg.seq_num > 0)
                            {
                                if (!lobbyData.downloaded_seq_nums.acknowledges(static_cast<std::uint64_t>(parsedMsg.seq_num)))
                                {
                                    lobbyData.downloaded_seq_nums.selective.push_back(static_cast<std::uint64_t>(parsedMsg.seq_num));
                                }
                            }
                            else if (parsedMsg.direction == MessageDirection::OUT && parsedMsg.seq_num > lobbyData.my_seq_num)
                            {
                                lobbyData.my_seq_num = parsedMsg.seq_num;
                            }
                        }
                        catch (const std::exception &e)
                        {
                            SPDLOG_ERROR("Failed to parse history message: {}", e.what());
                        }
                    }
                }
                lobbyData.downloaded_seq_nums.compress();
            }
        }

        return lobbyData;
    }
    catch (const std::exception &e)
    {
        SPDLOG_ERROR("Error loading lobby data from {}: {} (Wrong Master Password "
                     "or corrupted data?)",
                     filePath.string(), e.what());
        return std::unexpected(StoreError::Unknown);
    }
}

void LobbyStore::populateLobbies()
{
    std::filesystem::path lobbiesDir = std::filesystem::path(m_storeDir) / "lobbies";
    if (!std::filesystem::exists(lobbiesDir))
        return;

    auto isEncFile = [](const auto &entry)
    { return entry.path().string().ends_with(".enc") && !entry.path().string().ends_with(".history.enc"); };

    for (const auto &entry : std::filesystem::directory_iterator(lobbiesDir) | std::views::filter(isEncFile))
    {
        auto dataRes = loadLobbyDataFromFile(entry.path());
        if (dataRes && !dataRes->isEmpty())
        {
            auto lobby = std::make_shared<Lobby>(*dataRes);
            m_lobbyListener->addObserver(lobby);
        }
    }
}

std::shared_ptr<LobbyObserver> LobbyStore::getLobby(const std::string &lobbyName)
{
    return m_lobbyListener->getObserver(lobbyName);
}

std::shared_ptr<LobbyObserver> LobbyStore::getOrCreateLobby(const std::string &lobbyName)
{
    return m_lobbyListener->getOrCreateObserver(lobbyName, [&]() {
        SPDLOG_INFO("Creating new lobby: {}", lobbyName);
        LobbyData lobbyData;
        lobbyData.lobby_name = lobbyName;
        return std::make_shared<Lobby>(lobbyData);
    });
}

std::vector<std::shared_ptr<LobbyObserver>> LobbyStore::getLobbies()
{
    return m_lobbyListener->getObservers();
}

void LobbyStore::updateMasterPassword(const std::string &newPassword)
{
    std::promise<void> promise;
    std::future<void> future = promise.get_future();

    m_ioWorker.enqueue([this, newPassword, &promise]()
                       {
        try {
            std::filesystem::path vaultPath = std::filesystem::path(m_storeDir) / "vault.enc";
            std::filesystem::path tmpVaultPath = vaultPath.string() + ".tmp";

            std::vector<unsigned char> newSalt(16);
            if (RAND_bytes(newSalt.data(), static_cast<int>(newSalt.size())) != 1) {
                throw std::runtime_error("RAND_bytes failed");
            }

            std::vector<unsigned char> newKek = CryptoUtils::deriveStoreKey(newPassword, newSalt);
            
            std::vector<unsigned char> currentDek = m_masterKey;

            auto encryptedDekRes = CryptoUtils::encryptAES256GCM(currentDek, newKek);
            OPENSSL_cleanse(newKek.data(), newKek.size());

            if (!encryptedDekRes) {
                throw std::runtime_error("Failed to encrypt DEK with new password");
            }

            std::vector<unsigned char> vaultData;
            vaultData.insert(vaultData.end(), newSalt.begin(), newSalt.end());
            vaultData.insert(vaultData.end(), encryptedDekRes->begin(), encryptedDekRes->end());

            std::ofstream outFile(tmpVaultPath, std::ios::binary);
            if (!outFile || !outFile.write(reinterpret_cast<const char*>(vaultData.data()), vaultData.size())) {
                if (std::filesystem::exists(tmpVaultPath)) {
                    std::filesystem::remove(tmpVaultPath);
                }
                throw std::runtime_error("Failed to write new vault.tmp");
            }
            outFile.close();
            if (outFile.fail()) {
                if (std::filesystem::exists(tmpVaultPath)) {
                    std::filesystem::remove(tmpVaultPath);
                }
                throw std::runtime_error("Failed to close new vault.tmp");
            }

            std::error_code permEc;
            std::filesystem::permissions(tmpVaultPath, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace, permEc);

            // Atomic rename guarantees the old vault is overwritten safely
            std::filesystem::rename(tmpVaultPath, vaultPath);

            // Cleanup old vault_salt.bin if it still exists from old version
            std::filesystem::path oldSaltPath = std::filesystem::path(m_storeDir) / "vault_salt.bin";
            if (std::filesystem::exists(oldSaltPath)) {
                std::filesystem::remove(oldSaltPath);
            }

            SPDLOG_INFO("Master password updated instantly via DEK re-encryption.");
            promise.set_value();
        } catch (...) {
            promise.set_exception(std::current_exception());
        } });

    try
    {
        future.get();
    }
    catch (const std::exception &e)
    {
        SPDLOG_ERROR("Failed to update master password: {}", e.what());
        throw; // Rethrow to propagate to the UI
    }
}
