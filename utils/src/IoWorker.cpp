#include "IoWorker.h"

#include <sched.h>
#include <spdlog/spdlog.h>

IoWorker::IoWorker()
{
    m_worker = std::jthread(&IoWorker::workerLoop, this);
}

IoWorker::~IoWorker()
{
    stopAndDrain();
}

void IoWorker::stopAndDrain()
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stopRequested = true;
    }
    m_queueCv.notify_all();
    if (m_worker.joinable())
    {
        m_worker.request_stop();
        m_worker.join();
    }
}

void IoWorker::enqueue(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_taskQueue.push(std::move(task));
    }
    m_queueCv.notify_one();
}

void IoWorker::workerLoop(std::stop_token stoken)
{
    std::thread::id threadId = std::this_thread::get_id();
    int coreId = sched_getcpu();
    SPDLOG_INFO("IoWorker thread started. Thread ID: {}, Core: {}",
                std::hash<std::thread::id>{}(threadId), coreId);

    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [this, &stoken]()
                           { return !m_taskQueue.empty() || m_stopRequested || stoken.stop_requested(); });

            if ((m_stopRequested || stoken.stop_requested()) && m_taskQueue.empty())
            {
                break;
            }

            if (!m_taskQueue.empty())
            {
                task = std::move(m_taskQueue.front());
                m_taskQueue.pop();
            }
        }

        if (task)
        {
            int currentCore = sched_getcpu();
            SPDLOG_DEBUG("IoWorker executing task on Thread ID: {}, Core: {}",
                         std::hash<std::thread::id>{}(std::this_thread::get_id()), currentCore);
            try
            {
                task();
            }
            catch (const std::exception &e)
            {
                SPDLOG_ERROR("IoWorker: Unhandled exception in task: {}", e.what());
            }
            catch (...)
            {
                SPDLOG_ERROR("IoWorker: Unknown exception in task");
            }
        }
    }
}
