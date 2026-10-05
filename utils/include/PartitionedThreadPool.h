#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

/// A thread pool where tasks are partitioned by a string key (e.g. lobby name).
/// All tasks with the same key are guaranteed to execute on the same thread,
/// ensuring serialization per key while maintaining parallelism across keys.
///
/// Shutdown policy: shutdown() closes every queue under its own lock (new work is
/// rejected), lets the task currently running on each worker finish, cancels the
/// tasks still queued, and joins the workers. It is idempotent and is also called
/// by the destructor.
class PartitionedThreadPool
{
  public:
    explicit PartitionedThreadPool(size_t numThreads);
    ~PartitionedThreadPool();

    PartitionedThreadPool(const PartitionedThreadPool &) = delete;
    PartitionedThreadPool &operator=(const PartitionedThreadPool &) = delete;

    /// Enqueue a task partitioned by key. Tasks with the same key always run
    /// on the same worker thread, guaranteeing serial execution per key.
    /// Returns false (task rejected and destroyed) once the pool is closed.
    /// The [[nodiscard]] attribute ensures the caller checks this return value 
    /// to handle cases where the task couldn't be queued (e.g. during shutdown).
    [[nodiscard]] bool enqueue(const std::string &partitionKey, std::function<void()> task);

    /// Close the pool, cancel pending tasks and join the workers.
    void shutdown();

  private:
    struct WorkerState
    {
        std::queue<std::function<void()>> taskQueue;
        std::mutex queueMutex;
        std::condition_variable queueCv;
        bool closed = false;
    };

    // Workers own a reference to their state so that a worker which must be
    // detached (shutdown triggered from one of its own tasks) never touches freed memory.
    std::vector<std::shared_ptr<WorkerState>> m_workers;
    std::vector<std::thread> m_threads;
    size_t m_numThreads;
    std::once_flag m_shutdownOnce;

    static void workerLoop(std::shared_ptr<WorkerState> worker, int workerId);
};
