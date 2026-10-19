#pragma once
#include <algorithm>
#include <spdlog/spdlog.h>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include "CryptoUtils.h"
#include "SecureBuffer.h"

// Append-only authenticated transactions. AAD binds each record to its position.
// The file lock prevents two processes from allocating the same sequence.
class EncryptedJournal {
public:
    struct Location { off_t offset; std::uint32_t size; std::uint64_t index; };
    explicit EncryptedJournal(const std::filesystem::path& path) {
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd_ < 0) throw std::runtime_error("Cannot open transaction journal");
        if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            ::close(fd_); fd_ = -1;
            throw std::runtime_error("Store already open in another process");
        }
        const auto parent = path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
        const int dir = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dir < 0 || ::fsync(dir) != 0) {
            if (dir >= 0) ::close(dir);
            ::close(fd_); fd_ = -1;
            throw std::runtime_error("Cannot sync journal directory");
        }
        ::close(dir);
    }
    ~EncryptedJournal() { if (fd_ >= 0) ::close(fd_); }
    EncryptedJournal(const EncryptedJournal&) = delete;
    EncryptedJournal& operator=(const EncryptedJournal&) = delete;

    void replay(const std::vector<unsigned char>& key,
                const std::function<void(std::span<const unsigned char>, Location)>& consume) {
        ::lseek(fd_, 0, SEEK_SET);
        index_ = 0;
        for (;;) {
            const auto start = ::lseek(fd_, 0, SEEK_CUR);
            if (start < 0) throw std::runtime_error("Journal seek failed");
            std::array<unsigned char, 4> header{};
            const auto got = readBytes(header);
            if (got == 0) return;
            if (got != header.size()) { repairTail(start); return; }
            const std::uint32_t size = std::uint32_t(header[0]) | (std::uint32_t(header[1]) << 8)
                | (std::uint32_t(header[2]) << 16) | (std::uint32_t(header[3]) << 24);
            if (size < 28 || size > maxRecord) throw std::runtime_error("Invalid journal record length");
            std::vector<unsigned char> ciphertext(size);
            if (readBytes(ciphertext) != ciphertext.size()) { repairTail(start); return; }
            const auto recordAad = aad(index_);
            auto plain = CryptoUtils::decryptAES256GCM(ciphertext, key, recordAad.span());
            if (!plain) throw std::runtime_error("Journal authentication failed; refusing silent data loss");
            ScopedCleanse plainGuard(*plain);
            consume(*plain, {start + 4, size, index_});
            ++index_;
        }
    }
    Location append(std::span<const unsigned char> plain, const std::vector<unsigned char>& key, bool durable = true) {
        if (failed_) throw std::runtime_error("Journal requires reopening after an I/O failure");
        if (plain.size() > maxRecord - 28) throw std::runtime_error("Journal transaction too large");
        const auto recordAad = aad(index_);
        auto ciphertext = CryptoUtils::encryptAES256GCM(plain, key, recordAad.span());
        if (!ciphertext) throw std::runtime_error(ciphertext.error());
        const auto size = static_cast<std::uint32_t>(ciphertext->size());
        const std::array<unsigned char, 4> header{static_cast<unsigned char>(size),
            static_cast<unsigned char>(size >> 8), static_cast<unsigned char>(size >> 16), static_cast<unsigned char>(size >> 24)};
        const auto start = ::lseek(fd_, 0, SEEK_END);
        if (start < 0) throw std::runtime_error("Journal seek failed");
        try {
            writeBytes(header); writeBytes(*ciphertext);
            if (durable && ::fsync(fd_) != 0) throw std::runtime_error("Journal fsync failed");
            return {start + 4, size, index_++};
        } catch (...) {
            failed_ = true;
            // Do not acknowledge an uncertain transaction. Recovery will validate it.
            throw;
        }
    }
    std::vector<unsigned char> read(Location location, const std::vector<unsigned char>& key) const {
        if (location.size < 28 || location.size > maxRecord) throw std::runtime_error("Invalid record location");
        std::vector<unsigned char> ciphertext(location.size);
        std::size_t offset = 0;
        while (offset < ciphertext.size()) {
            auto n = ::pread(fd_, ciphertext.data() + offset, ciphertext.size() - offset, location.offset + offset);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw std::runtime_error("History record read failed");
            offset += static_cast<std::size_t>(n);
        }
        const auto recordAad = aad(location.index);
        auto plain = CryptoUtils::decryptAES256GCM(ciphertext, key, recordAad.span());
        if (!plain) throw std::runtime_error("History record authentication failed");
        return std::move(*plain);
    }

private:
    static constexpr std::uint32_t maxRecord = 4 * 1024 * 1024;
    int fd_{-1};
    std::uint64_t index_{};
    bool failed_{};

    struct AadBuffer {
        std::array<unsigned char, 40> data{};
        std::size_t size{};
        [[nodiscard]] std::span<const unsigned char> span() const noexcept {
            return {data.data(), size};
        }
    };
    static AadBuffer aad(std::uint64_t index) noexcept {
        static constexpr std::string_view prefix = "AnoDropJournal/v1/";
        AadBuffer buf{};
        std::copy(prefix.begin(), prefix.end(), buf.data.begin());
        char* first = reinterpret_cast<char*>(buf.data.data() + prefix.size());
        char* last = reinterpret_cast<char*>(buf.data.data() + buf.data.size());
        auto [ptr, ec] = std::to_chars(first, last, index);
        buf.size = prefix.size() + static_cast<std::size_t>(ptr - first);
        return buf;
    }
    std::size_t readBytes(std::span<unsigned char> bytes) {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto n = ::read(fd_, bytes.data() + offset, bytes.size() - offset);
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) throw std::runtime_error("Journal read failed");
            if (n == 0) break;
            offset += static_cast<std::size_t>(n);
        }
        return offset;
    }
    void writeBytes(std::span<const unsigned char> bytes) {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto n = ::write(fd_, bytes.data() + offset, bytes.size() - offset);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw std::runtime_error("Journal write failed");
            offset += static_cast<std::size_t>(n);
        }
    }
    void repairTail(off_t offset) {
        if (::ftruncate(fd_, offset) != 0 || ::fsync(fd_) != 0)
            throw std::runtime_error("Cannot repair incomplete journal tail");
        SPDLOG_WARN("[STORE] Incomplete journal tail repaired offset={}", offset);
    }
};
