#pragma once
#include "IoWorker.h"
#include <future>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace ui {
// Owns blocking chat work. Jobs never capture the ViewModel; its destructor
// drains this worker before the rest of the ViewModel is destroyed.
class ChatTaskWorker {
public:
    template<class Function>
    auto submit(Function work) -> std::future<std::invoke_result_t<Function>> {
        using Result = std::invoke_result_t<Function>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::move(work));
        auto result = task->get_future();
        if (!worker_.enqueue([task] { (*task)(); }))
            throw std::runtime_error("Chat task queue closed or full");
        return result;
    }
private:
    IoWorker worker_;
};
}
