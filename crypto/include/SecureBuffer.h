#pragma once

#include <openssl/crypto.h>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <span>
#include <vector>

/**
 * @brief Move-aware, RAII-governed secure string/buffer.
 *
 * Guarantees zeroization via OPENSSL_cleanse() on destruction, reassignment,
 * and clear() calls. Protects cryptographic keys, tokens, and shared secrets in memory.
 */
class SecureString
{
public:
    SecureString() = default;

    explicit SecureString(std::string s) : m_data(std::move(s)) {}
    SecureString(const char *s) : m_data(s ? s : "") {}
    SecureString(std::string_view sv) : m_data(sv) {}
    SecureString(std::span<const unsigned char> bytes)
        : m_data(reinterpret_cast<const char *>(bytes.data()), bytes.size()) {}

    ~SecureString()
    {
        cleanse();
    }

    SecureString(SecureString &&other) noexcept
        : m_data(std::move(other.m_data))
    {
    }

    SecureString &operator=(SecureString &&other) noexcept
    {
        if (this != &other)
        {
            cleanse();
            m_data = std::move(other.m_data);
        }
        return *this;
    }

    SecureString(const SecureString &other)
        : m_data(other.m_data)
    {
    }

    SecureString &operator=(const SecureString &other)
    {
        if (this != &other)
        {
            cleanse();
            m_data = other.m_data;
        }
        return *this;
    }

    SecureString &operator=(std::string s)
    {
        cleanse();
        m_data = std::move(s);
        return *this;
    }

    SecureString &operator=(const char *s)
    {
        cleanse();
        m_data = s ? s : "";
        return *this;
    }

    SecureString &operator=(std::string_view sv)
    {
        cleanse();
        m_data = sv;
        return *this;
    }

    void cleanse() noexcept
    {
        if (!m_data.empty())
        {
            OPENSSL_cleanse(m_data.data(), m_data.size());
            m_data.clear();
        }
    }

    void clear() noexcept
    {
        cleanse();
    }

    [[nodiscard]] const std::string &str() const noexcept { return m_data; }
    [[nodiscard]] const char *c_str() const noexcept { return m_data.c_str(); }
    [[nodiscard]] const char *data() const noexcept { return m_data.data(); }
    [[nodiscard]] char *data() noexcept { return m_data.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_data.size(); }
    [[nodiscard]] std::size_t length() const noexcept { return m_data.length(); }
    [[nodiscard]] bool empty() const noexcept { return m_data.empty(); }

    operator const std::string &() const noexcept { return m_data; }
    operator std::string_view() const noexcept { return m_data; }

    bool operator==(const SecureString &other) const noexcept { return m_data == other.m_data; }
    bool operator==(const std::string &other) const noexcept { return m_data == other; }
    bool operator==(std::string_view other) const noexcept { return m_data == other; }
    bool operator==(const char *other) const noexcept { return m_data == (other ? other : ""); }

    auto operator<=>(const SecureString &other) const noexcept { return m_data <=> other.m_data; }
    auto operator<=>(const std::string &other) const noexcept { return m_data <=> other; }

    friend std::string operator+(const SecureString &lhs, const std::string &rhs) { return lhs.m_data + rhs; }
    friend std::string operator+(const std::string &lhs, const SecureString &rhs) { return lhs + rhs.m_data; }
    friend std::string operator+(const SecureString &lhs, const SecureString &rhs) { return lhs.m_data + rhs.m_data; }
    friend std::string operator+(const SecureString &lhs, const char *rhs) { return lhs.m_data + (rhs ? rhs : ""); }
    friend std::string operator+(const char *lhs, const SecureString &rhs) { return (lhs ? lhs : "") + rhs.m_data; }

private:
    std::string m_data;
};

// JSON conversion functions for nlohmann::json
inline void to_json(nlohmann::json &j, const SecureString &s)
{
    j = s.str();
}

inline void from_json(const nlohmann::json &j, SecureString &s)
{
    if (j.is_string())
    {
        s = j.get<std::string>();
    }
    else
    {
        s = "";
    }
}

using SecureBuffer = SecureString;
