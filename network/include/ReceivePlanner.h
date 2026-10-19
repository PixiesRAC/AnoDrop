#pragma once
#include "LobbyData.h"
#include <chrono>
#include <map>
#include <vector>

// Exploration progresses independently of the contiguous receipt frontier.
// One slot retries old holes; the remaining budget explores newer sequences.
class ReceivePlanner {
public:
    using Clock = std::chrono::steady_clock;
    std::vector<std::int64_t> plan(std::int64_t announced, const AckState& received,
                                   std::size_t budget, Clock::time_point now) {
        std::vector<std::int64_t> result;
        if (announced <= 0 || budget == 0) return result;
        std::erase_if(attempts_, [&](const auto& item) { return received.acknowledges(static_cast<std::uint64_t>(item.first)); });
        const auto frontier = static_cast<std::int64_t>(std::min(received.through, static_cast<std::uint64_t>(announced)));
        if (frontier == announced) return result;
        auto offer = [&](std::int64_t seq) {
            if (received.acknowledges(static_cast<std::uint64_t>(seq)) ||
                std::find(result.begin(), result.end(), seq) != result.end()) return;
            auto it = attempts_.find(seq);
            if (it == attempts_.end() || now >= it->second.nextAttempt) result.push_back(seq);
        };
        // A budget of one alternates retry and discovery instead of starving either.
        const bool retryFirst = budget > 1 || retryTurn_;
        retryTurn_ = !retryTurn_;
        if (retryFirst) {
            auto seq = frontier;
            for (std::size_t examined = 0; seq < announced && examined < scanLimit && result.empty(); ++examined)
                offer(++seq);
        }
        if (cursor_ < frontier || cursor_ >= announced) cursor_ = frontier;
        for (std::size_t examined = 0; result.size() < budget && examined < scanLimit; ++examined) {
            if (cursor_ >= announced) cursor_ = frontier;
            offer(++cursor_);
            if (cursor_ == announced) break;
        }
        return result;
    }
    void accepted(std::int64_t seq, Clock::time_point now) {
        auto it = attempts_.find(seq);
        if (it == attempts_.end()) {
            if (attempts_.size() >= scanLimit) attempts_.erase(std::prev(attempts_.end()));
            it = attempts_.emplace(seq, Attempt{}).first;
        }
        auto& attempt = it->second;
        const auto delay = std::min(10 * (1 << attempt.exponent), 42);
        attempt.exponent = std::min(attempt.exponent + 1, 3);
        attempt.nextAttempt = now + std::chrono::seconds(delay);
    }
private:
    static constexpr std::size_t scanLimit = 4096;
    struct Attempt { Clock::time_point nextAttempt{}; int exponent{}; };
    std::map<std::int64_t, Attempt> attempts_;
    std::int64_t cursor_{};
    bool retryTurn_{};
};
