#include "rl_agent.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// ------------------------------------------------------------
// RandomAgent Implementation
// ------------------------------------------------------------

RandomAgent::RandomAgent(unsigned int seed) : rng_(seed) {}

std::string RandomAgent::selectAction(const json &observation,
                                      const std::string & /*targetItem*/,
                                      int /*targetQty*/, bool /*training*/) {
  std::vector<std::string> validActions;
  validActions.push_back("wait");

  if (observation.contains("buttons") && observation["buttons"].is_array()) {
    for (const auto &btn : observation["buttons"]) {
      if (btn.value("clickable", false)) {
        size_t idx = btn.value("i", 0);
        validActions.push_back("click(" + std::to_string(idx) + ")");
      }
    }
  }

  std::uniform_int_distribution<size_t> dist(0, validActions.size() - 1);
  return validActions[dist(rng_)];
}

// ------------------------------------------------------------
// QLearningAgent Implementation
// ------------------------------------------------------------

QLearningAgent::QLearningAgent(double alpha, double gamma, double epsilon,
                               double epsilonDecay, double minEpsilon,
                               unsigned int seed)
    : alpha_(alpha), gamma_(gamma), epsilon_(epsilon),
      epsilonDecay_(epsilonDecay), minEpsilon_(minEpsilon), rng_(seed) {}

std::string QLearningAgent::extractStateKey(const json &observation,
                                            const std::string &targetItem,
                                            int targetQty) const {
  std::string screen = observation.value("screen", "unknown");
  bool isPopup = observation.value("isPopup", false);
  int currentQty = observation.value("qty", 0);
  std::string title = observation.value("title", "");

  // Summarize clickable button text signatures for state discrimination
  std::string btnSummary;
  if (observation.contains("buttons") && observation["buttons"].is_array()) {
    for (const auto &btn : observation["buttons"]) {
      if (btn.value("clickable", false)) {
        std::string txt = btn.value("text", "");
        btnSummary += "[" + txt + "]";
      }
    }
  }
  std::string cartSummary;
  if (observation.contains("cart") && observation["cart"].is_array()) {
    for (const auto &item : observation["cart"]) {
      cartSummary += item.value("id", std::string{}) + "x" +
                     std::to_string(item.value("qty", 0)) + ",";
    }
  }
  return "goal:" + targetItem + "x" + std::to_string(targetQty) +
         "|scr:" + screen + "|pop:" + (isPopup ? "1" : "0") +
         "|pid:" + observation.value("productId", std::string{}) +
         "|pick:" + std::to_string(observation.value("qty", 0)) +
         "|cart:" + cartSummary + "|btns:" + btnSummary;
}

std::vector<std::string>
QLearningAgent::getAvailableActions(const json &observation) const {
  std::vector<std::string> actions;
  actions.push_back("wait");

  if (observation.contains("buttons") && observation["buttons"].is_array()) {
    for (const auto &btn : observation["buttons"]) {
      if (btn.value("clickable", false)) {
        size_t idx = btn.value("i", 0);
        actions.push_back("click(" + std::to_string(idx) + ")");
      }
    }
  }
  return actions;
}

double QLearningAgent::getQ(const std::string &state,
                            const std::string &action) const {
  auto stateIt = qTable_.find(state);
  if (stateIt != qTable_.end()) {
    auto actIt = stateIt->second.find(action);
    if (actIt != stateIt->second.end()) {
      return actIt->second;
    }
  }
  // if state-action pair is not found in the Q-table, return 0.0
  return 0.0;
}

double QLearningAgent::getMaxQ(const std::string &state,
                               const std::vector<std::string> &actions) const {
  if (actions.empty())
    return 0.0;
  double maxVal = -1e9;
  for (const auto &act : actions) {
    maxVal = std::max(maxVal, getQ(state, act));
  }
  return (maxVal == -1e9) ? 0.0 : maxVal;
}

std::string QLearningAgent::selectAction(const json &observation,
                                         const std::string &targetItem,
                                         int targetQty, bool training) {
  std::vector<std::string> actions = getAvailableActions(observation);
  if (actions.empty()) {
    return "wait";
  }

  std::string stateKey = extractStateKey(observation, targetItem, targetQty);

  // Exploration: Epsilon-Greedy
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  if (training && dist(rng_) < epsilon_) {
    std::uniform_int_distribution<size_t> actDist(0, actions.size() - 1);
    return actions[actDist(rng_)];
  }

  // Exploitation: Greedy choice (breaking ties randomly)
  double bestVal = -1e9;
  std::vector<std::string> bestActions;

  for (const auto &act : actions) {
    double qVal = getQ(stateKey, act);
    if (qVal > bestVal) {
      bestVal = qVal;
      bestActions.clear();
      bestActions.push_back(act);
    } else if (qVal == bestVal) {
      bestActions.push_back(act);
    }
  }

  std::uniform_int_distribution<size_t> actDist(0, bestActions.size() - 1);
  return bestActions[actDist(rng_)];
}

void QLearningAgent::update(const std::string &state, const std::string &action,
                            double reward, const std::string &nextState,
                            const json &nextObs, bool done) {
  double currentQ = getQ(state, action);
  double nextMaxQ = 0.0;
  if (!done) {
    std::vector<std::string> nextActions = getAvailableActions(nextObs);
    nextMaxQ = getMaxQ(nextState, nextActions);
  }

  // Bellman update
  double target = reward + gamma_ * nextMaxQ;
  double newQ = currentQ + alpha_ * (target - currentQ);
  qTable_[state][action] = newQ;

  // Log Q-value change
  // std::cout << "    [Q-Update] S: \"" << state.substr(0, 60) << "...\""
  //           << " | A: " << action
  //           << " | R: " << reward
  //           << " | Q: " << currentQ << " -> " << newQ << "\n";
}

void QLearningAgent::resetEpisode() {
  epsilon_ = std::max(minEpsilon_, epsilon_ * epsilonDecay_);
}

void QLearningAgent::saveQTable(const std::string &filename) const {
  json j = qTable_;
  std::ofstream file(filename);
  if (file.is_open()) {
    file << j.dump(2);
  }
}

void QLearningAgent::loadQTable(const std::string &filename) {
  std::ifstream file(filename);
  if (file.is_open()) {
    json j;
    file >> j;
    qTable_ =
        j.get<std::unordered_map<std::string,
                                 std::unordered_map<std::string, double>>>();
  }
}
