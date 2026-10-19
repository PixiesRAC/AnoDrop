#include "IoWorker.h"

#include <spdlog/spdlog.h>

IoWorker::IoWorker() : m_worker([state = m_state]
                                { workerLoop(state); })
{
}

IoWorker::~IoWorker()
{
    if (isCurrentWorker())
    {
        // State survives until the draining worker exits; it never accesses this.
        {
            std::lock_guard lock(m_state->mutex);
            m_state->closed = true;
        }
        m_worker.detach();
    }
    else
        stopAndDrain();
}

void IoWorker::stopAndDrain()
{
    {
        std::lock_guard lock(m_state->mutex);
        m_state->closed = true;
    }
    m_state->ready.notify_all();
    if (isCurrentWorker())
        return;
    std::call_once(m_joinOnce, [this]
                   { if (m_worker.joinable()) m_worker.join(); });
}

bool IoWorker::enqueue(std::function<void()> task)
{
    {
        std::lock_guard lock(m_state->mutex);
        if (m_state->closed || m_state->tasks.size() >= 1024)
            return false;
        m_state->tasks.push(std::move(task));
    }
    m_state->ready.notify_one();
    return true;
}

void IoWorker::workerLoop(std::shared_ptr<State> state)
{
    s_current = state.get();
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock lock(state->mutex);
            state->ready.wait(lock, [&]
                              { return state->closed || !state->tasks.empty(); });
            if (state->tasks.empty())
                break;
            task = std::move(state->tasks.front());
            state->tasks.pop();
        }
        try
        {
            if (task)
                task();
        }
        catch (const std::exception &e)
        {
            SPDLOG_ERROR("[BOOT] IoWorker task failed: {}", e.what());
        }
        catch (...)
        {
            SPDLOG_ERROR("[BOOT] IoWorker task threw a non-standard exception");
        }
    }
    s_current = nullptr;
}
