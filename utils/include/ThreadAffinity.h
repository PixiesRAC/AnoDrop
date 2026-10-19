#pragma once
#include <stdexcept>
#include <thread>

// Lifecycle operations belong to the constructing thread. Work submission may
// have a different concurrency contract; this guard does not serialize tasks.
class ThreadAffinity
{
  public:
    void requireOwner() const
    {
        if (std::this_thread::get_id() != m_owner)
            throw std::logic_error("Lifecycle operation must run on the owner thread");
    }

  private:
    const std::thread::id m_owner{std::this_thread::get_id()};
};
