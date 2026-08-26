#pragma once

#include <libtorrent/alert.hpp>

class AlertObserver
{
  public:
    AlertObserver() = default;
    virtual ~AlertObserver() = default;

    virtual void onAlert(lt::alert *alert) = 0;
};
