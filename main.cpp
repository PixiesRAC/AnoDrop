#include "core/DhtPublisher.h"
#include "network/DhtManager.h"
#include "ui/GuiBuilder.h"
#include "utils/Logger.h"

#include <memory>
#include <spdlog/spdlog.h>

int main() {
  Logger::init();
  GuiBuilder gui;
  gui.showLoadingScreen();
  
  std::unique_ptr<DhtManager> dhtManager = std::make_unique<DhtManager>();
  std::unique_ptr<DhtPublisher> dhtPublisher = std::make_unique<DhtPublisher>();

  if (dhtManager->isSessionReady()) { // One session per run for now
    dhtManager->startAlertThread();
    dhtManager->waitUntilDhtIsReady();
    dhtPublisher->setDhtManager(std::move(dhtManager));
    gui.setDhtPublisher(std::move(dhtPublisher));
  } else {
    spdlog::error("Failed to create session");
    return 1;
  }

  gui.buildAndRun();
  return 0;
}
