#include "agent.h"
#include "params.h"
#include "utils.h"
#include <nlohmann/json.hpp>

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

/*
Tear down the current tab, and start a new episode.
- Increment episode counter.
- Reset step counter.
- Close current tab if it exists.
- Create new tab.
- Attach to new tab.
- Enable Page and Runtime domains.
- Navigate to the given task URL with the seed.
*/
json Agent::reset(const std::string &task, int seed) {
  ++episode_;
  stepNumber_ = 0;
  goalMap_.clear();
  prevCartCount_ = 0;
  prevTargetQty_ = 0;

  currentTask_ = task;
  currentSeed_ = seed;
  if (!sessionId_.empty()) {
    try {
      cdp_->command("Target.closeTarget", {{"targetId", targetId_}});
    } catch (...) {
      // The old target may already be gone.
    }
  }

  // start from the blank page, redirect to test url later
  json target = cdp_->command("Target.createTarget", {{"url", "about:blank"}});
  targetId_ = target["result"]["targetId"].get<std::string>();

  // attach to the new tab, flatten adds the session_id by default
  json attach = cdp_->command("Target.attachToTarget",
                              {{"targetId", targetId_}, {"flatten", true}});
  sessionId_ = attach["result"]["sessionId"].get<std::string>();

  // enable Page and Runtime domains
  cdp_->command("Page.enable", {}, sessionId_);
  cdp_->command("Runtime.enable", {}, sessionId_);
  cdp_->command("Page.setLifecycleEventsEnabled", {{"enabled", true}},
                sessionId_);

  // reset popup flag, doesn't really do anything though. Might remove it soon.
  cdp_->clearPopupFlag();

  std::string url = task;

  // parse query parameters safely
  auto getParam = [&url](const std::string &key) -> std::string {
    std::string pattern = key + "=";
    size_t pos = url.find(pattern);
    if (pos == std::string::npos)
      return "";
    size_t start = pos + pattern.length();
    size_t end = url.find('&', start);
    return (end == std::string::npos) ? url.substr(start)
                                      : url.substr(start, end - start);
  };

  std::string item = getParam("item");
  std::string qtyStr = getParam("qty");

  if (!item.empty() && !qtyStr.empty()) {
    try {
      goalMap_[item] = std::stoi(qtyStr);
    } catch (...) {
    }
  }

  // navigate to the test url
  cdp_->command("Page.navigate", {{"url", url}}, sessionId_);

  // Wait for the initial page to settle, but do not
  // wait indefinitely.
  waitForPage();
  json observation = observe();
  return observation;
}

/*
Execute a single step.
- Increment step counter.
- Parse action: must be "wait" or "click(index)".
- If "wait": sleep for WAIT_MS.
- If "click(index)":
  - Re-discover all visible buttons.
  - Validate index is in range.
  - Validate button is clickable.
  - Dispatch a real mouse click.
- Wait until the page becomes stable (STEP_TIMEOUT_MS).
- Observe the new page state.
- Determine if the order is complete.
- Return StepResult with reward, done, observation, info.
*/
StepResult Agent::step(const std::string &action) {
  const auto start = std::chrono::steady_clock::now();

  StepResult result;
  stepNumber_++;

  const bool wasPopup = cdp_->popupShowing();

  try {
    if (stepNumber_ > MAX_STEPS) {
      result.reward = -10.0;
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
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
    // Reward Calculation (Event / Transition based)
    // ------------------------------------------------
    bool success = isOrderComplete();

    if (success) {
      result.reward = 20.0; // High reward for complete goal success
      result.done = true;
      result.info["reason"] = "correct order placed";
    } else if (stepNumber_ >= MAX_STEPS) {
      result.reward = -10.0;
      result.done = true;
      result.timedOut = true;
      result.info["reason"] = "maximum step count reached";
    } else {
      result.done = false;
      double stepReward = -0.5; // Small step penalty to encourage efficiency

      // Check current cart state
      int currentCartCount = 0;
      int currentTargetQty = 0;

      if (result.observation.contains("cart") &&
          result.observation["cart"].is_array()) {
        for (const auto &item : result.observation["cart"]) {
          std::string itemId = item.value("id", "");
          int itemQty = item.value("qty", 0);
          currentCartCount += itemQty;

          auto it = goalMap_.find(itemId);
          if (it != goalMap_.end()) {
            currentTargetQty += itemQty;
          }
        }
      }

      // 1. One-time reward for adding TARGET item to cart
      int targetDelta = currentTargetQty - prevTargetQty_;
      if (targetDelta > 0) {
        for (const auto &[targetItem, expectedQty] : goalMap_) {
          if (currentTargetQty == expectedQty) {
            stepReward += 3.0; // Exact target quantity reached
          } else if (currentTargetQty < expectedQty) {
            stepReward += 1.5; // Making progress towards target quantity
          } else {
            stepReward -= 0.8; // Over target quantity
          }
        }
      }

      // 2. Minor reward/penalty for non-target items added
      int otherDelta = (currentCartCount - currentTargetQty) -
                       (prevCartCount_ - prevTargetQty_);
      if (otherDelta > 0) {
        stepReward +=
            0.05; // Minor exploratory reward for learning how to add to cart
      }

      // 3. Checkout screen reached
      std::string screen = result.observation.value("screen", "");
      if (screen == "done") {
        stepReward += 0.08; // Small reward for reaching and completing checkout
        result.done = true;
      }

      prevCartCount_ = currentCartCount;
      prevTargetQty_ = currentTargetQty;

      result.reward = stepReward;
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

void Agent::connectToChromium() {
  std::string version;

  for (int i = 0; i < 100; ++i) {
    try {
      version =
          http_get("127.0.0.1", std::to_string(CDP_PORT), "/json/version");
      break;
    } catch (...) {
      if (i == 99)
        throw;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  json versionJson = json::parse(version);
  std::string browserWs =
      versionJson.at("webSocketDebuggerUrl").get<std::string>();
  WebSocketEndpoint endpoint = parse_ws_url(browserWs);
  cdp_ =
      std::make_unique<CDPClient>(endpoint.host, endpoint.port, endpoint.path);
}

/*
Returns the screen content (the text inside [data-screen] element),
button indices, whether order is complete, etc.  See observe_js_script.txt.
*/
json Agent::observe() {
  json result;

  // Using one JS expression only to READ the DOM.
  // It does not click anything.
  const std::string script = readScript("observe_js_script.txt");

  json evaluation = cdp_->command(
      "Runtime.evaluate",
      {{"expression", script}, {"returnByValue", true}, {"awaitPromise", true}},
      sessionId_);

  json remote = evaluation["result"]["result"];

  if (evaluation.contains("exceptionDetails")) {
    throw std::runtime_error(evaluation["exceptionDetails"].dump());
  }
  if (!remote.contains("value")) {
    throw std::runtime_error("observe failed: " + remote.dump());
  }

  json page = remote["value"];
  result["screen"] = (page.contains("screen") && page["screen"].is_string())
                         ? page["screen"].get<std::string>()
                         : std::string{};
  result["title"] = (page.contains("title") && page["title"].is_string())
                        ? page["title"].get<std::string>()
                        : std::string{};
  result["productId"] =
      (page.contains("productId") && page["productId"].is_string())
          ? page["productId"].get<std::string>()
          : std::string{};
  result["buttons"] = json::array();

  result["isPopup"] = page.contains("isPopup") &&
                      page["isPopup"].is_boolean() &&
                      page["isPopup"].get<bool>();
  result["orderComplete"] = page.contains("orderComplete") &&
                            page["orderComplete"].is_boolean() &&
                            page["orderComplete"].get<bool>();
  result["qty"] = (page.contains("qty") && page["qty"].is_number())
                      ? page["qty"].get<int>()
                      : 0;
  result["cart"] = (page.contains("cart") && page["cart"].is_array())
                       ? page["cart"]
                       : json::array();
  const auto &pageButtons = page["buttons"];
  for (size_t i = 0; i < pageButtons.size(); ++i) {
    const auto &b = pageButtons[i];
    result["buttons"].push_back({{"i", i},
                                 {"text", b.value("text", std::string{})},
                                 {"clickable", b.value("clickable", false)}});
  }
  return result;
}

/*
Returns all the buttons that are clickable and visible on the screen.

Returns: x, y, width, height of the button and whether it is clickable.
*/
std::vector<Agent::Button> Agent::discoverButtons() {
  const std::string script = readScript("find_buttons_js.txt");

  json response = cdp_->command(
      "Runtime.evaluate", {{"expression", script}, {"returnByValue", true}},
      sessionId_);

  const json &values = response["result"]["result"]["value"];

  std::vector<Button> result;

  for (const auto &value : values) {
    Button b;
    b.clickable = value.value("clickable", false);
    b.x = value.value("x", 0.0);
    b.y = value.value("y", 0.0);
    b.width = value.value("width", 0.0);
    b.height = value.value("height", 0.0);
    result.push_back(b);
  }

  return result;
}

/*
Performs a real mouse click on the button.
*/
void Agent::realMouseClick(double x, double y, double width, double height) {
  double cx = x + width / 2.0;
  double cy = y + height / 2.0;

  cdp_->command(
      "Input.dispatchMouseEvent",
      {{"type", "mouseMoved"}, {"x", cx}, {"y", cy}, {"button", "none"}},
      sessionId_);

  cdp_->command("Input.dispatchMouseEvent",
                {{"type", "mousePressed"},
                 {"x", cx},
                 {"y", cy},
                 {"button", "left"},
                 {"clickCount", 1}},
                sessionId_);
  cdp_->command("Input.dispatchMouseEvent",
                {{"type", "mouseReleased"},
                 {"x", cx},
                 {"y", cy},
                 {"button", "left"},
                 {"clickCount", 1}},
                sessionId_);
}

/*
Returns true if the order is complete and the ordered items match goalMap_.
Checks:
- The screen is "done" (document.body attribute data-screen == "done")
- The items placed in the order match all items & quantities in goalMap_
*/
bool Agent::isOrderComplete() {
  const std::string script = R"JS(
(() => {
    const isDone = document.body.getAttribute("data-screen") === "done" ||
                   document.querySelector("[data-screen='done']") !== null;
    if (!isDone) {
        return { isDone: false, order: null };
    }

    const order = (window.miniShop && window.miniShop.order) ? window.miniShop.order : [];
    return {
        isDone: true,
        order: order
    };
})()
)JS";

  json response = cdp_->command(
      "Runtime.evaluate", {{"expression", script}, {"returnByValue", true}},
      sessionId_);

  json val = response["result"]["result"]["value"];
  if (!val.value("isDone", false)) {
    return false;
  }

  // Extract ordered items from the JS state: array of {id: string, qty: int}
  std::unordered_map<std::string, int> orderMap;
  if (val.contains("order") && val["order"].is_array()) {
    for (const auto &item : val["order"]) {
      std::string id = item.value("id", "");
      int qty = item.value("qty", 0);
      if (!id.empty()) {
        orderMap[id] += qty;
      }
    }
  }

  // Match against goalMap_
  if (orderMap.size() != goalMap_.size()) {
    return false;
  }

  for (const auto &[item, expectedQty] : goalMap_) {
    auto it = orderMap.find(item);
    if (it == orderMap.end() || it->second != expectedQty) {
      return false;
    }
  }

  return true;
}

void Agent::waitForPage() {
  // The controller has a hard upper bound. We do not
  // depend on a particular page's network behavior.
  for (int i = 0; i < 20; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    try {
      observe();
      return;
    } catch (...) {
      // Document may not yet exist.
    }
  }
}

/*
Waits for the page to be stable for a given timeout in milliseconds.
*/
bool Agent::waitUntilStable(int timeoutMs) {
  auto start = std::chrono::steady_clock::now();

  // We deliberately don't require "network idle".
  // Interactive pages can keep WebSockets/polling alive
  // indefinitely.
  while (true) {
    try {
      observe();
      return true;
    } catch (...) {
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();

    if (elapsed >= timeoutMs)
      return false;

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
}

void Agent::writeLog(const std::string &action, const StepResult &result,
                     long long elapsed, bool popup) {
  json line = {{"episode", episode_},
               {"seed", currentSeed_},
               {"step", stepNumber_},
               {"action", action},
               {"observation", result.observation},
               {"reward", result.reward},
               {"done", result.done},
               {"time_ms", elapsed},
               {"popup", popup},
               {"timed_out", result.timedOut},
               {"info", result.info}};

  log_ << line.dump() << '\n';
  log_.flush();
}
