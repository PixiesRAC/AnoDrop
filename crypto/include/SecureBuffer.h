#pragma once
#include <openssl/crypto.h>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

// Immutable shared secret: copying a handle never duplicates the bytes.
// Moves transfer ownership; the final owner wipes the storage.
// TODO: Consider backing Storage with OPENSSL_secure_malloc / mlock to prevent
// memory pages containing plaintext secrets from being swapped to disk by the kernel.
class SecureString {
    struct Storage {
        explicit Storage(std::string_view value) : bytes(value) {}
        ~Storage() { if (!bytes.empty()) OPENSSL_cleanse(bytes.data(), bytes.size()); }
        Storage(const Storage&) = delete;
        Storage& operator=(const Storage&) = delete;
        std::string bytes;
    };
    std::shared_ptr<const Storage> value_;
public:
    SecureString() = default;
    SecureString(std::string value) : value_(std::make_shared<Storage>(value)) {
        if (!value.empty()) OPENSSL_cleanse(value.data(), value.size());
    }
    SecureString(const char* value) : SecureString(std::string_view(value ? value : "")) {}
    SecureString(std::string_view value) : value_(std::make_shared<Storage>(value)) {}
    SecureString(std::span<const unsigned char> value)
        : SecureString(std::string_view(reinterpret_cast<const char*>(value.data()), value.size())) {}
    SecureString(const SecureString&) = default;
    SecureString& operator=(const SecureString&) = default;
    SecureString(SecureString&&) noexcept = default;
    SecureString& operator=(SecureString&&) noexcept = default;
    SecureString& operator=(std::string value) { *this = SecureString(std::move(value)); return *this; }
    SecureString& operator=(std::string_view value) { *this = SecureString(value); return *this; }
    SecureString& operator=(const char* value) { *this = SecureString(value); return *this; }
    void clear() noexcept { value_.reset(); }
    // Releases this handle; bytes are wiped only when the final owner releases them.
    void reset() noexcept { clear(); }
    const std::string& str() const noexcept { static const std::string empty; return value_ ? value_->bytes : empty; }
    const char* data() const noexcept { return str().data(); }
    const char* c_str() const noexcept { return str().c_str(); }
    std::size_t size() const noexcept { return str().size(); }
    std::size_t length() const noexcept { return size(); }
    bool empty() const noexcept { return size() == 0; }
    bool starts_with(std::string_view prefix) const noexcept { return std::string_view(str()).starts_with(prefix); }
    bool starts_with(char c) const noexcept { return std::string_view(str()).starts_with(c); }
    std::string_view substr(std::size_t pos = 0, std::size_t count = std::string_view::npos) const noexcept {
        return std::string_view(str()).substr(pos, count);
    }
    operator const std::string&() const noexcept { return str(); }
    operator std::string_view() const noexcept { return str(); }
    bool operator==(const SecureString& rhs) const noexcept {
        if (size() != rhs.size()) return false;
        return CRYPTO_memcmp(data(), rhs.data(), size()) == 0;
    }
    bool operator==(const std::string& rhs) const noexcept {
        if (size() != rhs.size()) return false;
        return CRYPTO_memcmp(data(), rhs.data(), size()) == 0;
    }
    bool operator==(const char* rhs) const noexcept {
        if (!rhs) return empty();
        return std::string_view(str()) == std::string_view(rhs);
    }
    friend bool operator==(const char* lhs, const SecureString& rhs) noexcept {
        return rhs == lhs;
    }
    friend bool operator==(const std::string& lhs, const SecureString& rhs) noexcept {
        return rhs == lhs;
    }
    auto operator<=>(const SecureString& rhs) const noexcept { return str() <=> rhs.str(); }
    friend std::string operator+(const SecureString& a, const std::string& b) { return a.str() + b; }
    friend std::string operator+(const SecureString& a, const char* b) { return a.str() + b; }
    friend std::string operator+(const std::string& a, const SecureString& b) { return a + b.str(); }
    friend std::string operator+(const char* a, const SecureString& b) { return std::string(a ? a : "") + b.str(); }
};
inline void to_json(nlohmann::json& j, const SecureString& value) { j = value.str(); }
inline void from_json(const nlohmann::json& j, SecureString& value) { value = j.get<std::string>(); }

template <>
struct std::formatter<SecureString> : std::formatter<std::string_view> {
    auto format(const SecureString& s, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(std::string_view(s), ctx);
    }
};

// Guard for a stable buffer: construct after its final allocation, never resize
// or move that buffer while the guard is alive.
class ScopedCleanse {
public:
    explicit ScopedCleanse(std::span<unsigned char> bytes) : bytes_(bytes) {}
    explicit ScopedCleanse(std::string& text)
        : bytes_(reinterpret_cast<unsigned char*>(text.data()), text.size()) {}
    ~ScopedCleanse() { if (!bytes_.empty()) OPENSSL_cleanse(bytes_.data(), bytes_.size()); }
    ScopedCleanse(const ScopedCleanse&) = delete;
    ScopedCleanse& operator=(const ScopedCleanse&) = delete;
private:
    std::span<unsigned char> bytes_;
};
