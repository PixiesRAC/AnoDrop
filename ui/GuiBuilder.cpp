#include "ui/GuiBuilder.h"

#include <ncurses.h>

GuiBuilder::GuiBuilder() : m_running(false), m_selectedOption(1) {
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
}

GuiBuilder::~GuiBuilder() { endwin(); }

void GuiBuilder::setDhtPublisher(
    std::unique_ptr<DhtPublisher> rendezvousService) {
  m_dhtPublisher = std::move(rendezvousService);
}

void GuiBuilder::showLoadingScreen() {
  clear();
  mvprintw(2, 4, "=== AnoDrop: Decentralized Messaging ===");
  mvprintw(4, 4, "Bootstrapping DHT network... Please wait...");
  refresh();
}

void GuiBuilder::buildAndRun() {
  m_running = true;

  while (m_running) {
    showMainMenu();
    int choice = getch();
    if (choice == KEY_UP) {
      if (m_selectedOption > 1)
        m_selectedOption--;
    } else if (choice == KEY_DOWN) {
      if (m_selectedOption < 2)
        m_selectedOption++;
    } else if (choice == 10) { // Enter key
      if (m_selectedOption == 2) {
        m_running = false;
      } else if (m_selectedOption == 1) {
        handlePublicRendezvous();
      }
    } else if (choice == 'q' || choice == 'Q') {
      m_running = false;
    }
  }
}

void GuiBuilder::showMainMenu() {
  clear();
  mvprintw(2, 4, "=== AnoDrop: Decentralized Messaging ===");

  if (m_selectedOption == 1)
    attron(A_REVERSE);
  mvprintw(4, 4, "1. Public Rendezvous (Password-Based Generation)");
  if (m_selectedOption == 1)
    attroff(A_REVERSE);

  if (m_selectedOption == 2)
    attron(A_REVERSE);
  mvprintw(5, 4, "2. Exit");
  if (m_selectedOption == 2)
    attroff(A_REVERSE);

  mvprintw(7, 4, "Use ARROWS to navigate, ENTER to select.");
  refresh();
}

void GuiBuilder::handlePublicRendezvous() {
  clear();
  mvprintw(2, 4, "=== Public Rendezvous ===");
  mvprintw(4, 4, "Enter Channel Name (Passphrase): ");

  echo();
  curs_set(1);
  char channelName[256];
  getnstr(channelName, 255);
  noecho();
  curs_set(0);

  std::string passphrase(channelName);

  try {
    auto info = m_dhtPublisher->initializeRendezvous(passphrase);

    mvprintw(7, 4, "DHT Public Key Target: %s", info.pubKeyHex.c_str());
    mvprintw(8, 4, "DHT Target ID (SHA-1): %s", info.targetIDHex.c_str());

    std::string message = "Hello from AnoDrop Initial Room!";
    m_dhtPublisher->dispatchMessage(message);

    mvprintw(10, 4, "Message dispatched to DHT (BEP 44 mutable item).");
    mvprintw(11, 4, "Waiting for confirmation");
    refresh();

    m_dhtPublisher->waitForPutConfirmed();

    mvprintw(12, 4, "[SUCCESS] DHT Put operation completed!");
  } catch (const std::exception &e) {
    mvprintw(6, 4, "Error: %s", e.what());
  }

  mvprintw(14, 4, "Press any key to return to menu...");
  refresh();
  getch();
}
