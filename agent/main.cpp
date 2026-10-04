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

// 12 Goals (4 items x 3 quantities)
// const std::vector<Goal> ALL_GOALS = {
//   {"blue-mug", 1}, {"blue-mug", 2}, {"blue-mug", 3},
//   {"red-hat", 1}, {"red-hat", 2}, {"red-hat", 3},
//   {"green-lamp", 1}, {"green-lamp", 2}, {"green-lamp", 3},
//   {"yellow-notebook", 1}, {"yellow-notebook", 2}, {"yellow-notebook", 3}
// };

const std::vector<Goal> ALL_GOALS = {{"yellow-notebook", 1}};

// const std::vector<int> SEEDS = {42, 101, 2024};
const std::vector<int> SEEDS = {42};

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
                bool training) {
  std::string url = buildUrl(goal.item, goal.qty, seed);
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

void evaluateAgent(Agent &env, BaseRLAgent &rlAgent,
                   const std::string &agentName, int testSeed) {
  int totalSuccesses = 0;
  int totalTrials = ALL_GOALS.size();

  std::cout << "\n--- Evaluating " << agentName << " (Test Seed: " << testSeed
            << ") ---\n";

  for (const auto &goal : ALL_GOALS) {
    bool passed = runEpisode(env, rlAgent, goal, testSeed, /*training=*/false);
    std::cout << "  Goal: " << std::setw(15) << std::left << goal.item << " x "
              << goal.qty << " -> " << (passed ? "PASS [✓]" : "FAIL [✗]")
              << "\n";
    if (passed)
      totalSuccesses++;
  }

  double successRate = (100.0 * totalSuccesses) / totalTrials;
  std::cout << ">> " << agentName << " Result: " << totalSuccesses << "/"
            << totalTrials << " (" << std::fixed << std::setprecision(1)
            << successRate << "% success rate)\n";
}

int main(int argc, char *argv[]) {
  try {
    std::string chrome_path =
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
    const std::string chromium = argc >= 2 ? argv[1] : chrome_path;
    const std::string logFile = argc >= 3 ? argv[2] : "agent.jsonl";

    std::cout << "====================================================\n";
    std::cout << "  CDP Shopping Agent: Baseline vs Tabular Q-Learning\n";
    std::cout << "====================================================\n";

    Agent env(chromium, logFile);

    // 1. Evaluate Random Agent Baseline
    std::cout << "\n===============================";
    std::cout << "\n  1. Random Agent (Baseline)";
    std::cout << "\n===============================\n";
    RandomAgent randomAgent(42);
    evaluateAgent(env, randomAgent, "RandomAgent", /*testSeed=*/42);

    // 2. Train Q-Learning Agent across 3 different seeds
    std::cout << "\n===============================";
    std::cout << "\n  2. Tabular Q-Learning Agent";
    std::cout << "\n===============================\n";

    QLearningAgent qAgent(/*alpha=*/0.25, /*gamma=*/0.95, /*epsilon=*/0.6,
                          /*epsilonDecay=*/0.95, /*minEpsilon=*/0.05, 42);

    for (size_t runIdx = 0; runIdx < SEEDS.size(); ++runIdx) {
      int trainSeed = SEEDS[runIdx];
      std::cout << "\n[Training Run " << (runIdx + 1) << "/3 with Seed "
                << trainSeed << "]\n";

      // Training loop: Train over all 12 goals for multiple epochs
      const int EPOCHS = 100;
      for (int epoch = 1; epoch <= EPOCHS; ++epoch) {
        int epochSuccess = 0;
        for (const auto &goal : ALL_GOALS) {
          bool ok = runEpisode(env, qAgent, goal, trainSeed, /*training=*/true);
          if (ok)
            epochSuccess++;
        }
        std::cout << "  Epoch " << epoch << "/" << EPOCHS
                  << " - Training successes: " << epochSuccess << "/"
                  << ALL_GOALS.size() << "\n";
      }

      // Test Q-Learning Agent on unseen test seed
      int testSeed = trainSeed + 500;
      evaluateAgent(env, qAgent,
                    "QLearningAgent (Run " + std::to_string(runIdx + 1) + ")",
                    testSeed);
    }

    std::cout << "\nAll runs completed successfully!\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "fatal: " << e.what() << '\n';
    return 1;
  }
}
