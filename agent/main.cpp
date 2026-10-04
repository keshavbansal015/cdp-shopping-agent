#include <chrono>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "agent.h"
#include "params.h"
#include "rl_agent.h"
#include "utils.h"

using json = nlohmann::json;

struct Goal {
  std::string item;
  int qty;
};

// Full 12 Goals (4 items x 3 quantities)
const std::vector<Goal> ALL_GOALS = {
    {"blue-mug", 1},        {"blue-mug", 2},        {"blue-mug", 3},
    {"red-hat", 1},         {"red-hat", 2},         {"red-hat", 3},
    {"green-lamp", 1},      {"green-lamp", 2},      {"green-lamp", 3},
    {"yellow-notebook", 1}, {"yellow-notebook", 2}, {"yellow-notebook", 3}};

const std::vector<int> SEEDS = {42, 101, 2024};
const std::vector<double> POPUP_VALUES = {0.0, 0.15, 0.4};

std::string buildUrl(const std::string &item, int qty, int seed,
                     double popup_p = 0.10, double delay_p = 0.05) {
  return "file:///Users/keshavbansal/keshav/dev_test/cdp-shopping-agent/site/"
         "index.html?seed=" +
         std::to_string(seed) + "&item=" + item +
         "&qty=" + std::to_string(qty) + "&popup_p=" + std::to_string(popup_p) +
         "&delay_p=" + std::to_string(delay_p);
}

// Run single episode for an agent
bool runEpisode(Agent &env, BaseRLAgent &rlAgent, const Goal &goal, int seed,
                bool training, double popup_p) {
  std::string url = buildUrl(goal.item, goal.qty, seed, popup_p);
  json obs = env.reset(url, seed);

  std::string stateKey = rlAgent.extractStateKey(obs, goal.item, goal.qty);
  bool success = false;

  for (int step = 0; step < MAX_STEPS; ++step) {
    std::string action =
        rlAgent.selectAction(obs, goal.item, goal.qty, training);
    StepResult result = env.step(action);
    std::string nextStateKey =
        rlAgent.extractStateKey(result.observation, goal.item, goal.qty);
    if (training) {
      rlAgent.update(stateKey, action, result.reward, nextStateKey,
                     result.observation, result.done);
    }
    stateKey = nextStateKey;
    obs = result.observation;
    if (result.done) {
      success = (result.reward > 0);
      break;
    }
  }

  if (training) {
    rlAgent.resetEpisode();
  }

  return success;
}

// Evaluate agent across multiple attempts to reach >= 200 trials
void evaluateAgentExtensive(Agent &env, BaseRLAgent &rlAgent,
                            const std::string &agentName, double popup_p,
                            int targetAttempts = 200) {
  int totalSuccesses = 0;
  int totalTrials = 0;
  int seedBase = 1000;

  std::cout << "\n--- Evaluating " << agentName << " (Popup_p: " << popup_p
            << ", Target: " << targetAttempts << " attempts) ---\n";

  while (totalTrials < targetAttempts) {
    int currentSeed = seedBase + totalTrials;
    for (const auto &goal : ALL_GOALS) {
      if (totalTrials >= targetAttempts)
        break;
      bool passed = runEpisode(env, rlAgent, goal, currentSeed,
                               /*training=*/false, popup_p);
      if (passed)
        totalSuccesses++;
      totalTrials++;
    }
  }

  double successRate = (100.0 * totalSuccesses) / totalTrials;
  std::cout << ">> " << agentName << " Result: " << totalSuccesses << "/"
            << totalTrials << " (" << std::fixed << std::setprecision(1)
            << successRate << "% success rate)\n";
}

// 1. Run Random Agent Experiment
void runRandomAgentExperiment(const std::string &chromium,
                              const std::string &logFile) {
  std::cout << "\n===============================";
  std::cout << "\n  1. Random Agent (Baseline)";
  std::cout << "\n===============================\n";

  Agent env(chromium, logFile);
  RandomAgent randomAgent(42);

  // Run at least 200 attempts as requested
  evaluateAgentExtensive(env, randomAgent, "RandomAgent", /*popup_p=*/0.10,
                         200);
}

// 2. Run Q-Learning Agent Experiment (Training across seeds & testing different
// popup probabilities)
void runQLearningExperiment(const std::string &chromium,
                            const std::string &logFile) {
  std::cout << "\n===============================";
  std::cout << "\n  2. Tabular Q-Learning Agent";
  std::cout << "\n===============================\n";

  Agent env(chromium, logFile);

  for (size_t runIdx = 0; runIdx < SEEDS.size(); ++runIdx) {
    int trainSeed = SEEDS[runIdx];
    std::cout << "\n[Training Run " << (runIdx + 1) << "/" << SEEDS.size()
              << " with Seed " << trainSeed << "]\n";

    QLearningAgent qAgent(/*alpha=*/0.25, /*gamma=*/0.95, /*epsilon=*/0.6,
                          /*epsilonDecay=*/0.95, /*minEpsilon=*/0.05,
                          trainSeed);

    // Training loop over epochs
    const int EPOCHS = 20; // Adjust as needed to ensure enough training logs
    for (int epoch = 1; epoch <= EPOCHS; ++epoch) {
      int epochSuccess = 0;
      for (const auto &goal : ALL_GOALS) {
        bool ok = runEpisode(env, qAgent, goal, trainSeed, /*training=*/true,
                             /*popup_p=*/0.10);
        if (ok)
          epochSuccess++;
      }
      std::cout << "  Epoch " << epoch << "/" << EPOCHS
                << " - Training successes: " << epochSuccess << "/"
                << ALL_GOALS.size() << "\n";
    }

    // Test Q-Learning Agent with different popup_p values (0, 0.15, 0.4)
    for (double popup_p : POPUP_VALUES) {
      std::string agentLabel = "QLearningAgent (Run " +
                               std::to_string(runIdx + 1) +
                               ", popup_p=" + std::to_string(popup_p) + ")";
      evaluateAgentExtensive(env, qAgent, agentLabel, popup_p, 200);
    }
  }
}

int main(int argc, char *argv[]) {
  try {
    std::string chrome_path =
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
    const std::string chromium = argc >= 2 ? argv[1] : chrome_path;

    std::cout << "====================================================\n";
    std::cout << "  CDP Shopping Agent: Baseline vs Tabular Q-Learning\n";
    std::cout << "====================================================\n";

    // Separate log files for Random Agent and Q-Learning Agent
    std::string randomLogFile = "random_agent.jsonl";
    std::string qLogFile = "q_learning_agent.jsonl";

    // Run Random Agent and save to its own log file
    runRandomAgentExperiment(chromium, randomLogFile);

    // Run Q-Learning Agent and save to its own log file
    runQLearningExperiment(chromium, qLogFile);

    std::cout << "\nAll experiments and logging completed successfully!\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "fatal: " << e.what() << '\n';
    return 1;
  }
}