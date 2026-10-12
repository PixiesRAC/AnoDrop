#pragma once

#include <string>
#include <vector>

namespace NetworkUtils
{
    /// Checks whether an interface exists, has IFF_UP and IFF_RUNNING, and has an assigned IP address.
    [[nodiscard]] bool isInterfaceUpAndRunning(const std::string &interfaceName);
}

