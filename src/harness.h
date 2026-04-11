#pragma once

#include "bot_process.h"
#include "hand.h"
#include <vector>
#include <string>

class Harness {
public:
    // Launch bot processes from the given commands.
    // commands[i] is the executable for seat i.
    explicit Harness(const std::vector<std::string>& commands, int timeout_ms = 10);
    ~Harness();

    // Get the BotIO callbacks wired to the bot processes.
    BotIO get_io();

    // Send GAME_START to each bot with their individual seat number.
    void send_game_start(int num_players, int starting_chips,
                         int preflop_mult, int flop_mult,
                         int turn_mult, int river_mult);

    // Kill all bot processes.
    void shutdown();

    int num_bots() const { return static_cast<int>(bots_.size()); }

private:
    std::vector<BotProcess> bots_;
    int timeout_ms_;
};
