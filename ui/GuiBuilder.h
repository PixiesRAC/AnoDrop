#pragma once

#include "core/DhtPublisher.h"

#include <memory>

class GuiBuilder {
public:
  GuiBuilder();
  ~GuiBuilder();

  void buildAndRun();
  void showLoadingScreen();
  void setDhtPublisher(std::unique_ptr<DhtPublisher> rendezvousService);

private:
  void showMainMenu();
  void handlePublicRendezvous();

  bool m_running;
  int m_selectedOption;
  std::unique_ptr<DhtPublisher> m_dhtPublisher;
};
