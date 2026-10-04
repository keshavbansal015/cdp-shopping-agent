#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <random>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Base Agent Interface
class BaseRLAgent {
public:
  virtual ~BaseRLAgent() = default;
  virtual std::string selectAction(const json &observation, const std::string &targetItem, int targetQty, bool training = true) = 0;
  virtual void update(const std::string &state, const std::string &action, double reward, const std::string &nextState, const json &nextObs, bool done) {}
  virtual void resetEpisode() {}
};

// Random Agent (Baseline)
class RandomAgent : public BaseRLAgent {
public:
  RandomAgent(unsigned int seed = 42);
  std::string selectAction(const json &observation, const std::string &targetItem, int targetQty, bool training = true) override;

private:
  std::mt19937 rng_;
};

// Tabular Q-Learning Agent
class QLearningAgent : public BaseRLAgent {
public:
  QLearningAgent(double alpha = 0.2, double gamma = 0.95, double epsilon = 0.6,
                 double epsilonDecay = 0.98, double minEpsilon = 0.05, unsigned int seed = 42);

  std::string selectAction(const json &observation, const std::string &targetItem, int targetQty, bool training = true) override;
  void update(const std::string &state, const std::string &action, double reward,
              const std::string &nextState, const json &nextObs, bool done) override;
  void resetEpisode() override;

  std::string extractStateKey(const json &observation, const std::string &targetItem, int targetQty) const;
  std::vector<std::string> getAvailableActions(const json &observation) const;

  void saveQTable(const std::string &filename) const;
  void loadQTable(const std::string &filename);

private:
  double alpha_;
  double gamma_;
  double epsilon_;
  double epsilonDecay_;
  double minEpsilon_;
  std::mt19937 rng_;

  // Q-table: State -> (Action -> Q-value)
  std::unordered_map<std::string, std::unordered_map<std::string, double>> qTable_;
  double getQ(const std::string &state, const std::string &action) const;
  double getMaxQ(const std::string &state, const std::vector<std::string> &actions) const;
};
