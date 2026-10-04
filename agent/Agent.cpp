#include "Agent.h"
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

json Agent::observe() {
  json result;

  // We use one JS expression only to READ the DOM.
  // It does not click anything.
  const std::string script = R"JS(
(() => {
    function visible(el) {
        const s = getComputedStyle(el);
        const r = el.getBoundingClientRect();

        return (
            s.display !== "none" &&
            s.visibility !== "hidden" &&
            parseFloat(s.opacity || "1") > 0 &&
            r.width > 0 &&
            r.height > 0 &&
            r.bottom >= 0 &&
            r.right >= 0 &&
            r.top <= window.innerHeight &&
            r.left <= window.innerWidth
        );
    }

    function text(el) {
        return (el.innerText || el.textContent || "")
            .replace(/\\s+/g, " ")
            .trim();
    }

    const screenElement =
        document.querySelector("[data-screen]");

    const goalElement =
        document.querySelector("[data-goal]");

    const elements =
        Array.from(
            document.querySelectorAll(
                "button, [role='button']"
            )
        );

    const buttons = [];

    for (const el of elements) {
        if (!visible(el))
            continue;

        const r =
            el.getBoundingClientRect();

        const disabled =
            el.disabled === true ||
            el.getAttribute("aria-disabled") === "true" ||
            el.hasAttribute("disabled");

        buttons.push({
            text: text(el),
            clickable: !disabled,
            x: r.left,
            y: r.top,
            width: r.width,
            height: r.height
        });
    }

    const success =
        document.querySelector(
            "[data-order-complete='true']"
        ) !== null;

    return {
        screen: screenElement
            ? text(screenElement)
            : "",
        goal: goalElement
            ? text(goalElement)
            : "",
        buttons: buttons,
        orderComplete: success
    };
})()
)JS";

  json evaluation = cdp_->command(
      "Runtime.evaluate",
      {{"expression", script}, {"returnByValue", true}, {"awaitPromise", true}},
      sessionId_);

  json remote = evaluation["result"]["result"];

  if (!remote.contains("value"))
    throw std::runtime_error("Could not read page observation");

  json page = remote["value"];
  result["screen"] = page.value("screen", std::string{});
  result["goal"] = page.value("goal", std::string{});
  result["buttons"] = json::array();

  const auto &pageButtons = page["buttons"];

  for (size_t i = 0; i < pageButtons.size(); ++i) {
    const auto &b = pageButtons[i];
    result["buttons"].push_back({{"i", i},
                                 {"text", b.value("text", std::string{})},
                                 {"clickable", b.value("clickable", false)}});
  }

  return result;
}

std::vector<Agent::Button> Agent::discoverButtons() {
  const std::string script = R"JS(
(() => {
    function visible(el) {
        const s = getComputedStyle(el);
        const r = el.getBoundingClientRect();

        return (
            s.display !== "none" &&
            s.visibility !== "hidden" &&
            parseFloat(s.opacity || "1") > 0 &&
            r.width > 0 &&
            r.height > 0 &&
            r.bottom >= 0 &&
            r.right >= 0 &&
            r.top <= window.innerHeight &&
            r.left <= window.innerWidth
        );
    }

    const result = [];

    for (const el of
         document.querySelectorAll(
             "button, [role='button']"
         )) {

        if (!visible(el))
            continue;

        const r =
            el.getBoundingClientRect();

        result.push({
            clickable:
                !el.disabled &&
                el.getAttribute("aria-disabled")
                    !== "true" &&
                !el.hasAttribute("disabled"),

            x: r.left,
            y: r.top,
            width: r.width,
            height: r.height
        });
    }

    return result;
})()
)JS";

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

bool Agent::isOrderComplete() {
  const std::string script = R"JS(
(() => {
    return document.querySelector(
        "[data-order-complete='true']"
    ) !== null;
})()
)JS";

  json response = cdp_->command(
      "Runtime.evaluate", {{"expression", script}, {"returnByValue", true}},
      sessionId_);

  return response["result"]["result"]["value"].get<bool>();
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
               {"goal", result.observation.value("goal", std::string{})},
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

std::string Agent::addSeedToUrl(std::string url, long long seed) {
  char separator = url.find('?') == std::string::npos ? '?' : '&';
  return url + separator + "seed=" + std::to_string(seed);
}