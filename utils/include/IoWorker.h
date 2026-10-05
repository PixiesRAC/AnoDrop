#pragma once

#include <condition_variable>
#include <functional>
#include <queue>
#include <thread>

class IoWorker
{
  public:
    IoWorker();
    ~IoWorker();
    
    void stopAndDrain();

    void enqueue(std::function<void()> task);

  private:
    std::queue<std::function<void()>> m_taskQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    bool m_stopRequested = false;
    std::jthread m_worker;

    void workerLoop();
};
