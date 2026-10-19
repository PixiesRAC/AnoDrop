#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "DhtChat.h"
#include "ChatTaskWorker.h"
#include "LobbyData.h"
#include "LobbyObserver.h"

namespace ui
{

struct FormattedMessageLine
{
    std::string textWithoutStatus;
    std::string statusText;
    int statusColorPair{0};
    int textColorPair{1};
    bool isBold{false};
    int displayX{2};
};

struct HeaderViewModel
{
    std::string title;
    std::string statusText;
    int statusColorPair{2};
    bool isPeerConnected{false};
    bool showConnectionStatus{false};
    std::string timezoneText;
};

class ChatViewModel
{
  public:
    ChatViewModel(std::shared_ptr<LobbyObserver> lobbyObserver, DhtChat *dhtChat);
    ~ChatViewModel() = default;

    [[nodiscard]] bool update();

    [[nodiscard]] const HeaderViewModel &getHeader() const noexcept { return m_header; }
    [[nodiscard]] const std::vector<FormattedMessageLine> &getVisibleLines() const noexcept { return m_visibleLines; }
    [[nodiscard]] const std::string &getStatusWarning() const noexcept { return m_statusWarning; }
    [[nodiscard]] const std::string &getCurrentInput() const noexcept { return m_currentInput; }
    [[nodiscard]] int getFocusIndex() const noexcept { return m_focusIndex; }
    [[nodiscard]] int getScrollOffset() const noexcept { return m_scrollOffset; }
    [[nodiscard]] int getMaxScroll() const noexcept { return m_maxScroll; }
    [[nodiscard]] bool hasMessages() const noexcept { return m_totalMessageCount > 0; }

    void setDimensions(int chatAreaHeight, int screenWidth);
    void cycleFocus();
    void setFocusIndex(int index);
    void scrollUp(int lines = 1);
    void scrollDown(int lines = 1);
    void pageUp();
    void pageDown();
    void appendInputChar(char c);
    void deleteLastInputChar();

    enum class EnterResult
    {
        None,
        ExitView,
        ResyncTriggered
    };
    [[nodiscard]] EnterResult handleEnter();

  private:
    void rebuildViewData();
    bool collectCompletedWork();
    void showSendError(SendError error);
    void triggerRateLimitWarning(int count);
    void indexNewMessages(std::span<const ChatMessage> messages);
    std::string formatMessagePrefix(const ChatMessage &msg, const LobbyViewData &lobbyData) const;

    std::shared_ptr<LobbyObserver> m_lobbyObserver;
    DhtChat *m_dhtChat;

    LobbyViewData m_lobbyData;
    HeaderViewModel m_header;
    std::vector<FormattedMessageLine> m_visibleLines;

    std::string m_currentInput;
    int m_focusIndex{0};
    int m_scrollOffset{0};
    int m_maxScroll{0};
    int m_chatAreaHeight{10};
    int m_screenWidth{80};
    size_t m_totalMessageCount{0};

    // Rate limit warning state
    std::string m_statusWarning;
    std::chrono::steady_clock::time_point m_statusWarningTime{};

    // Incremental resync indexing
    size_t m_lastIndexedMessageCount{0};
    std::map<std::int64_t, std::vector<std::int64_t>> m_origToResyncs;
    std::map<std::int64_t, std::int64_t> m_resyncToOrig;

    struct HistoryPage {
        std::size_t offset{}, count{}, total{}, indexOffset{};
        bool loadedVisible{};
        std::vector<ChatMessage> visible, toIndex;
    };
    std::future<HistoryPage> m_historyFuture;
    std::future<std::expected<std::int64_t, SendError>> m_sendFuture;
    std::future<int> m_resyncFuture;
    std::future<std::size_t> m_readFuture;
    std::string m_submittedInput;
    HistoryPage m_cachedPage;
    bool m_hasCachedPage{false};
    std::size_t m_markedReadCount{};
    bool m_lastConnectedState{false};
    bool m_dimensionsChanged{false};
    ChatTaskWorker m_tasks; // Declared last: drained first during destruction.
};

} // namespace ui
