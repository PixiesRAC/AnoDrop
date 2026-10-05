#include "PartitionedThreadPool.h"

#include <spdlog/spdlog.h>
#ifdef __linux__
#include <sched.h>
#endif

PartitionedThreadPool::PartitionedThreadPool(size_t numThreads) : m_numThreads(numThreads)
{
    m_workers.reserve(numThreads);
    m_threads.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i)
    {
        m_workers.push_back(std::make_shared<WorkerState>());
    }
    for (size_t i = 0; i < numThreads; ++i)
    {
        m_threads.emplace_back(&PartitionedThreadPool::workerLoop, m_workers[i], static_cast<int>(i));
    }
}

PartitionedThreadPool::~PartitionedThreadPool()
{
    shutdown();
}

void PartitionedThreadPool::shutdown()
{
    std::call_once(m_shutdownOnce, [this]()
                   {
        std::vector<std::queue<std::function<void()>>> cancelled;
        cancelled.reserve(m_workers.size());

        for (auto &worker : m_workers)
        {
            {
                std::lock_guard<std::mutex> lock(worker->queueMutex);
                worker->closed = true;
                cancelled.push_back(std::move(worker->taskQueue));
                worker->taskQueue = {};
            }
            worker->queueCv.notify_all();
        }

        const auto self = std::this_thread::get_id();
        for (auto &thread : m_threads)
        {
            if (!thread.joinable())
                continue;
            if (thread.get_id() == self)
            {
                // shutdown() reached from a task running on this very worker (e.g. it
                // released the last owner). Joining would deadlock: detach instead.
                // The worker holds its own shared_ptr<WorkerState> and never touches `this`.
                SPDLOG_WARN("PartitionedThreadPool::shutdown called from one of its workers; detaching it");
                thread.detach();
            }
            else
            {
                thread.join();
            }
        }

        std::size_t cancelledCount = 0;
        for (const auto &q : cancelled)
            cancelledCount += q.size();
        if (cancelledCount > 0)
        {
            SPDLOG_INFO("PartitionedThreadPool: {} pending task(s) cancelled at shutdown", cancelledCount);
        }
        // `cancelled` is destroyed here, on the thread calling shutdown(), after the
        // workers are gone: captured resources are released deterministically.
    });
}

bool PartitionedThreadPool::enqueue(const std::string &partitionKey, std::function<void()> task)
{
    size_t index = std::hash<std::string>{}(partitionKey) % m_numThreads;
    auto &worker = m_workers[index];
    {
        std::lock_guard<std::mutex> lock(worker->queueMutex);
        if (worker->closed)
        {
            SPDLOG_DEBUG("PartitionedThreadPool: task rejected for partition '{}' (pool closed)", partitionKey);
            return false;
        }
        worker->taskQueue.push(std::move(task));
    }
    // We use notify_one because each partition (queue) has exactly one dedicated worker thread.
    // There are no other threads waiting on this specific worker's condition variable.
    worker->queueCv.notify_one();
    return true;
}

void PartitionedThreadPool::workerLoop(std::shared_ptr<WorkerState> worker, int workerId)
{
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(worker->queueMutex);
            worker->queueCv.wait(lock, [&worker]
                                 { return worker->closed || !worker->taskQueue.empty(); });

            if (worker->closed)
            {
                return;
            }

            task = std::move(worker->taskQueue.front());
            worker->taskQueue.pop();
        }

        if (task)
        {
#ifdef __linux__
            SPDLOG_DEBUG("PartitionedThreadPool Worker {} executing task on CPU core {}", workerId, sched_getcpu());
#else
            SPDLOG_DEBUG("PartitionedThreadPool Worker {} executing task", workerId);
#endif
            // Exception boundary: a failing task must not terminate the whole process.
            try
            {
                task();
            }
            catch (const std::exception &e)
            {
                SPDLOG_ERROR("PartitionedThreadPool Worker {}: task threw: {}", workerId, e.what());
            }
        }
    }
}
