#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "DhtChat.h"
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
    bool isOutgoing{false};
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
    [[nodiscard]] size_t getTotalMessageCount() const noexcept { return m_totalMessageCount; }

    void setDimensions(int chatAreaHeight, int screenWidth);
    void cycleFocus();
    void setFocusIndex(int index);
    void scrollUp(int lines = 1);
    void scrollDown(int lines = 1);
    void pageUp();
    void pageDown();
    void appendInputChar(char c);
    void deleteLastInputChar();
    void clearInput();

    enum class EnterResult
    {
        None,
        ExitView,
        MessageSent,
        ResyncTriggered,
        RateLimitHit
    };
    [[nodiscard]] EnterResult handleEnter();

  private:
    void rebuildViewData();
    void triggerRateLimitWarning(int count);
    void indexNewMessages(std::span<const ChatMessage> messages);
    std::string formatMessagePrefix(const ChatMessage &msg, const LobbyData &lobbyData) const;

    std::shared_ptr<LobbyObserver> m_lobbyObserver;
    DhtChat *m_dhtChat;

    LobbyData m_lobbyData;
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

    bool m_lastConnectedState{false};
    bool m_dimensionsChanged{false};
};

} // namespace ui
