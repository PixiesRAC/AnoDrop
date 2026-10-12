#pragma once

// Domain-specific salt parameters structure.
// Each field contributes to the Argon2 salt via manual serialization.

inline void append_le32(std::vector<uint8_t> &vec, uint32_t val)
{
    vec.push_back(val & 0xFF);
    vec.push_back((val >> 8) & 0xFF);
    vec.push_back((val >> 16) & 0xFF);
    vec.push_back(static_cast<unsigned char>((val >> 24) & 0xFF));
}

inline void append_le64(std::vector<uint8_t> &vec, uint64_t val)
{
    append_le32(vec, static_cast<uint32_t>(val & 0xFFFFFFFF));
    append_le32(vec, static_cast<uint32_t>((val >> 32) & 0xFFFFFFFF));
}

struct SaltParams
{
    uint32_t version = 1;               // Salt derivation scheme version
    uint32_t domainId = 0xA20D;         // AnoDrop domain identifier
    uint32_t keyPurpose = 0x01;         // Purpose: DHT mutable item salt
    uint64_t epochMarker = 2026;        // Epoch marker for key rotation
    std::string extraValue = "AnoDrop"; // Extra configurable value to alter salt between software

    // Serializes the structure into raw bytes in Little-Endian format:
    // logicalSize || version || domainId || keyPurpose || epochMarker || extraValue
    [[nodiscard]] std::vector<uint8_t> serialize() const
    {
        std::vector<uint8_t> result;

        // Logical size: 4 (version) + 4 (domainId) + 4 (keyPurpose) + 8 (epochMarker) + extraValue length
        uint32_t logicalSize = 20 + static_cast<uint32_t>(extraValue.size());

        result.reserve(4 + logicalSize);

        // Include the payload size (useful for deserialization validation)
        append_le32(result, logicalSize);

        // Append fields in Little-Endian to ensure cross-platform compatibility
        append_le32(result, version);
        append_le32(result, domainId);
        append_le32(result, keyPurpose);
        append_le64(result, epochMarker);

        // Append extra string data
        if (!extraValue.empty())
        {
            result.insert(result.end(), extraValue.begin(), extraValue.end());
        }

        return result;
    }
};
