#pragma once

// Domain-specific salt parameters structure.
// Each field contributes to the Argon2 salt via manual serialization.

#include <cstdint>
#include <cstring>
#include <vector>

struct SaltParams
{
    uint32_t version = 1;        // Salt derivation scheme version
    uint32_t domainId = 0xA20D;  // AnoDrop domain identifier
    uint32_t keyPurpose = 0x01;  // Purpose: DHT mutable item salt
    uint64_t epochMarker = 2026; // Epoch marker for key rotation

    // Serializes the structure into raw bytes:
    // sizeof(SaltParams) || version || domainId || keyPurpose || epochMarker
    std::vector<uint8_t> serialize() const
    {
        std::vector<uint8_t> result;

        auto appendField = [&result](const auto &field)
        {
            const auto *ptr = reinterpret_cast<const uint8_t *>(&field);
            result.insert(result.end(), ptr, ptr + sizeof(field));
        };

        // First: sizeof(SaltParams) as a fixed-width value
        uint32_t structSize = static_cast<uint32_t>(sizeof(SaltParams));
        appendField(structSize);

        // Then: each field value as raw bytes
        appendField(version);
        appendField(domainId);
        appendField(keyPurpose);
        appendField(epochMarker);

        return result;
    }
};
