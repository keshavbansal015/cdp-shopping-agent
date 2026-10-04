#pragma once
#include "cdp_client.h"
#include "chromium_process.h"
#include "utils_structs.h"
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

class Agent {
public:
  std::unordered_map<std::string, int> goalMap_;
  Agent(const std::string &chromiumPath, const std::string &logPath);
  json reset(const std::string &task);
  StepResult step(const std::string &action);
  ~Agent();

private:
  struct Button {
    std::string text;

    bool clickable = false;

    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
  };

  Chromium chromium_;
  std::unique_ptr<CDPClient> cdp_;
  std::string targetId_;  // browser tab id
  std::string sessionId_; // session id
  std::ofstream log_;

  int episode_ = 0;
  int stepNumber_ = 0;
  std::string currentTask_;
  int currentSeed_;
  // Helper methods
  void connectToChromium();
  json observe();
  std::vector<Button> discoverButtons();
  void realMouseClick(double x, double y, double width, double height);
  bool isOrderComplete();
  void waitForPage();
  bool waitUntilStable(int timeoutMs);
  void writeLog(const std::string &action, const StepResult &result,
                long long elapsed, bool popup);
  // static std::string addSeedToUrl(std::string url, int seed);
};