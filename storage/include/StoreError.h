#pragma once
#include <stdexcept>

class StoreRecoveryRequired : public std::runtime_error {
public:
    StoreRecoveryRequired() : std::runtime_error("Storage outcome may be durable; reopen the store before retrying") {}
};

