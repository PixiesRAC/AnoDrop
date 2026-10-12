#include "NetworkUtils.h"

#include <algorithm>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <spdlog/spdlog.h>

namespace NetworkUtils
{

bool isInterfaceUpAndRunning(const std::string &interfaceName)
{
    if (interfaceName.empty() || interfaceName == "any")
    {
        return true;
    }

    struct ifaddrs *ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1)
    {
        return false;
    }

    bool found = false;
    for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_name && interfaceName == ifa->ifa_name)
        {
            unsigned int flags = ifa->ifa_flags;
            if ((flags & IFF_UP) && (flags & IFF_RUNNING))
            {
                if (ifa->ifa_addr && (ifa->ifa_addr->sa_family == AF_INET || ifa->ifa_addr->sa_family == AF_INET6))
                {
                    found = true;
                    break;
                }
            }
        }
    }

    freeifaddrs(ifaddr);
    return found;
}

} // namespace NetworkUtils

