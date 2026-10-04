#include "Agent.h"
#include "params.h"
#include "utils.h"
#include <iostream>
#include "utils.h"

using json = nlohmann::json;

Agent::Agent(const std::string &chromiumPath, const std::string &logPath)
    : log_(logPath, std::ios::out | std::ios::app) {

  if (!log_) {
    throw std::runtime_error("Failed to open log file: " + logPath);
  }
  chromium_.start(chromiumPath, 9222);
  connectToChromium();
}

Agent::~Agent() {
  cdp_.reset();
  chromium_.stop();
}

json Agent::reset(const std::string &task, long long seed) {
  ++episode_;
  stepNumber_ = 0;

  currentTask_ = task;
  currentSeed_ = seed;

  if (!sessionId_.empty()) {
    try {
      cdp_->command("Target.closeTarget", {{"targetId", targetId_}});
    } catch (...) {
      // The old target may already be gone.
    }
  }

  json target = cdp_->command("Target.createTarget", {{"url", "about:blank"}});

  targetId_ = target["result"]["targetId"].get<std::string>();

  json attach = cdp_->command("Target.attachToTarget",
                              {{"targetId", targetId_}, {"flatten", true}});

  sessionId_ = attach["result"]["sessionId"].get<std::string>();

  cdp_->command("Page.enable", {}, sessionId_);
  cdp_->command("Runtime.enable", {}, sessionId_);
  cdp_->command("Page.setLifecycleEventsEnabled", {{"enabled", true}},
                sessionId_);

  cdp_->clearPopupFlag();

  std::string url = addSeedToUrl(task, seed);

  cdp_->command("Page.navigate", {{"url", url}}, sessionId_);

  // Wait for the initial page to settle, but do not
  // wait indefinitely.
  waitForPage();

  json observation = observe();

  return observation;
}

StepResult Agent::step(const std::string &action) {
  const auto start = std::chrono::steady_clock::now();

  StepResult result;

  stepNumber_++;

  const bool wasPopup = cdp_->popupShowing();

  try {
    if (stepNumber_ > MAX_STEPS) {
      result.reward = 0;
      result.done = true;
      result.timedOut = true;

      result.observation = observe();

      result.info = {{"reason", "maximum step count reached"}};

      writeLog(action, result, elapsedMs(start), wasPopup);

      return result;
    }

    // ------------------------------------------------
    // Parse the only two allowed actions.
    // ------------------------------------------------

    if (action == "wait") {
      std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_MS));
    } else if (startsWith(action, "click(") && endsWith(action, ")")) {

      std::string inside = action.substr(6, action.size() - 7);

      size_t buttonIndex;

      try {
        size_t consumed = 0;

        long long parsed = std::stoll(inside, &consumed);

        if (consumed != inside.size() || parsed < 0) {
          throw std::runtime_error("invalid button index");
        }

        buttonIndex = static_cast<size_t>(parsed);
      } catch (...) {
        throw std::runtime_error("invalid click action: " + action);
      }

      // IMPORTANT:
      //
      // We re-read the page immediately before clicking.
      // Thus the index refers to the current observation,
      // not an old cached list.
      std::vector<Button> buttons = discoverButtons();

      if (buttonIndex >= buttons.size()) {
        throw std::runtime_error("button index out of range");
      }

      const Button &button = buttons[buttonIndex];

      if (!button.clickable) {
        throw std::runtime_error("button is not clickable");
      }

      realMouseClick(button.x, button.y, button.width, button.height);
    } else {
      throw std::runtime_error("action must be click(i) or wait");
    }

    // ------------------------------------------------
    // Give the page a short opportunity to react.
    // ------------------------------------------------

    if (!waitUntilStable(STEP_TIMEOUT_MS)) {
      result.timedOut = true;
      result.done = true;
      result.info = {{"reason", "step time limit exceeded"}};
    }

    result.observation = observe();

    // ------------------------------------------------
    // Reward.
    //
    // The only positive terminal reward corresponds
    // to the page's explicit success state.
    //
    // There is no reward for intermediate UI states.
    // ------------------------------------------------

    bool success = isOrderComplete();

    if (success) {
      result.reward = 1;
      result.done = true;

      result.info["reason"] = "correct order placed";
    } else if (stepNumber_ >= MAX_STEPS) {
      result.reward = 0;
      result.done = true;

      result.timedOut = true;

      result.info["reason"] = "maximum step count reached";
    } else {
      result.reward = 0;
      result.done = false;
    }
  } catch (const std::exception &e) {
    result.reward = 0;
    result.done = true;

    result.info = {{"error", e.what()}};

    try {
      result.observation = observe();
    } catch (...) {
      result.observation = {
          {"screen", ""}, {"goal", ""}, {"buttons", json::array()}};
    }
  }

  writeLog(action, result, elapsedMs(start), wasPopup || cdp_->popupShowing());

  return result;
}