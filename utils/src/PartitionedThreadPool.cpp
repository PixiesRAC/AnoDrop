#include "LogEvents.h"
#include "PartitionedThreadPool.h"

#include <algorithm>
#include <spdlog/spdlog.h>
#ifdef __linux__
#include <sched.h>
#endif

PartitionedThreadPool::PartitionedThreadPool(size_t numThreads, ThreadLauncher launch) : m_numThreads(numThreads)
{
    if (numThreads == 0)
        throw std::invalid_argument("Thread pool needs at least one worker");
    m_workers.reserve(numThreads);
    m_threads.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i)
    {
        m_workers.push_back(std::make_shared<WorkerState>());
    }
    if (!launch)
        launch = [](std::function<void()> task)
        { return std::thread(std::move(task)); };
    try
    {
        for (size_t i = 0; i < numThreads; ++i)
        {
            auto thread = launch([worker = m_workers[i], id = static_cast<int>(i)]
                                 { workerLoop(worker, id); });
            if (!thread.joinable())
                throw std::runtime_error("Thread launcher returned no worker");
            m_threads.push_back(std::move(thread));
        }
    }
    catch (...)
    {
        // The destructor is not invoked when construction fails. Wake and join
        // every worker already created before propagating the launch failure.
        for (auto &worker : m_workers)
        {
            {
                std::lock_guard lock(worker->queueMutex);
                worker->closed = true;
            }
            worker->queueCv.notify_all();
        }
        for (auto &thread : m_threads)
            if (thread.joinable())
                thread.join();
        throw;
    }
}

PartitionedThreadPool::~PartitionedThreadPool()
{
    shutdown();
    // If destruction was triggered by the last owner inside a task, shutdown
    // only requested closure. State remains owned by the detached worker.
    for (auto &thread : m_threads)
    {
        if (!thread.joinable())
            continue;
        if (thread.get_id() == std::this_thread::get_id())
            thread.detach();
        else
            thread.join();
    }
}

void PartitionedThreadPool::shutdown()
{
    // A worker must not wait on call_once while an external caller joins it.
    const bool fromWorker = std::ranges::any_of(m_workers, [](const auto &state)
                                                { return state.get() == s_currentWorker; });
    if (fromWorker)
    {
        for (auto &state : m_workers)
        {
            {
                std::lock_guard lock(state->queueMutex);
                state->closed = true;
            }
            state->queueCv.notify_all();
        }
        return;
    }
    std::call_once(m_shutdownOnce, [this]()
                   {
        std::size_t cancelledCount = 0;

        for (auto &worker : m_workers)
        {
            {
                std::lock_guard<std::mutex> lock(worker->queueMutex);
                worker->closed = true;
                cancelledCount += worker->taskQueue.size();
            }
            worker->queueCv.notify_all();
        }

        for (auto& thread : m_threads)
            if (thread.joinable()) thread.join();

        // Closed queues cannot receive new work. Destroy captures outside locks,
        // after joining workers, without allocating a cancellation container.
        for (auto& worker : m_workers)
            while (!worker->taskQueue.empty()) worker->taskQueue.pop();
        if (cancelledCount > 0)
        {
            SPDLOG_INFO("[BOOT] PartitionedThreadPool: {} pending task(s) cancelled at shutdown", cancelledCount);
        } });
}

bool PartitionedThreadPool::enqueue(const std::string &partitionKey, std::function<void()> task)
{
    size_t index = std::hash<std::string>{}(partitionKey) % m_numThreads;
    auto &worker = m_workers[index];
    {
        std::lock_guard<std::mutex> lock(worker->queueMutex);
        if (worker->closed || worker->taskQueue.size() >= 1024)
        {
            SPDLOG_DEBUG("[BOOT] PartitionedThreadPool: task rejected for partition {} (pool closed)", LogEvents::lobbyId(partitionKey));
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
    s_currentWorker = worker.get();
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(worker->queueMutex);
            worker->queueCv.wait(lock, [&worker]
                                 { return worker->closed || !worker->taskQueue.empty(); });

            if (worker->closed)
            {
                s_currentWorker = nullptr;
                return;
            }

            task = std::move(worker->taskQueue.front());
            worker->taskQueue.pop();
        }

        if (task)
        {
            // Exception boundary: a failing task must not terminate the whole process.
            try
            {
                task();
            }
            catch (const std::exception &e)
            {
                SPDLOG_ERROR("[BOOT] PartitionedThreadPool Worker {}: task threw: {}", workerId, e.what());
            }
            catch (...)
            {
                SPDLOG_ERROR("[BOOT] PartitionedThreadPool Worker {}: task threw a non-standard exception", workerId);
            }
        }
    }
}
