#include "utils/WorkerThread.h"

WorkerThread::WorkerThread()
{
    m_worker = std::jthread(&WorkerThread::workerLoop, this);
}

WorkerThread::~WorkerThread()
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

void WorkerThread::enqueue(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_taskQueue.push(std::move(task));
    }
    m_queueCv.notify_one();
}

void WorkerThread::workerLoop()
{
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [this]() { return !m_taskQueue.empty() || m_stopRequested || m_worker.get_stop_token().stop_requested(); });

            if ((m_stopRequested || m_worker.get_stop_token().stop_requested()) && m_taskQueue.empty())
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
            task();
        }
    }
}
