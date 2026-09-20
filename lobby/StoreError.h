#pragma once

#include <string>

enum class StoreError
{
    FileNotFound,
    FileOpenFailed,
    WriteFailed,
    ReadFailed,
    EncryptionFailed,
    DecryptionFailed,
    InvalidData,
    Unknown
};

inline std::string storeErrorToString(StoreError error)
{
    switch (error)
    {
    case StoreError::FileNotFound: return "FileNotFound";
    case StoreError::FileOpenFailed: return "FileOpenFailed";
    case StoreError::WriteFailed: return "WriteFailed";
    case StoreError::ReadFailed: return "ReadFailed";
    case StoreError::EncryptionFailed: return "EncryptionFailed";
    case StoreError::DecryptionFailed: return "DecryptionFailed";
    case StoreError::InvalidData: return "InvalidData";
    default: return "Unknown Error";
    }
}
