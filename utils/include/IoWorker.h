#pragma once
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

class IoWorker
{
  public:
    IoWorker();
    ~IoWorker();
    // Owner calls join; a task can request draining without joining itself.
    void stopAndDrain();
    [[nodiscard]] bool enqueue(std::function<void()> task);
    [[nodiscard]] bool isCurrentWorker() const noexcept
    {
        return s_current == m_state.get();
    }

  private:
    struct State
    {
        std::queue<std::function<void()>> tasks;
        std::mutex mutex;
        std::condition_variable ready;
        bool closed = false;
    };
    inline static thread_local State *s_current = nullptr;
    std::shared_ptr<State> m_state = std::make_shared<State>();
    std::thread m_worker;
    std::once_flag m_joinOnce;
    static void workerLoop(std::shared_ptr<State> state);
};
