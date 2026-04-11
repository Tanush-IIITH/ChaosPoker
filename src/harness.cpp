#include "harness.h"
#include <sstream>
#include <iostream>

Harness::Harness(const std::vector<std::string>& commands, int timeout_ms)
    : timeout_ms_(timeout_ms) {
    bots_.reserve(commands.size());
    for (const auto& cmd : commands) {
        bots_.emplace_back(cmd);
    }
}

Harness::~Harness() {
    shutdown();
}

BotIO Harness::get_io() {
    BotIO io;

    io.send = [this](int seat, const std::string& msg) {
        if (seat >= 0 && seat < num_bots()) {
            bots_[seat].write_line(msg);
        }
    };

    io.broadcast = [this](const std::string& msg) {
        for (int i = 0; i < num_bots(); i++) {
            bots_[i].write_line(msg);
        }
    };

    io.recv = [this](int seat) -> std::string {
        if (seat < 0 || seat >= num_bots()) return "";
        return bots_[seat].read_line(timeout_ms_);
    };

    return io;
}

void Harness::send_game_start(int num_players, int starting_chips,
                               int preflop_mult, int flop_mult,
                               int turn_mult, int river_mult) {
    for (int i = 0; i < num_bots(); i++) {
        std::ostringstream oss;
        oss << "GAME_START " << num_players
            << " " << i
            << " " << starting_chips
            << " " << preflop_mult
            << " " << flop_mult
            << " " << turn_mult
            << " " << river_mult;
        bots_[i].write_line(oss.str());
    }
}

void Harness::shutdown() {
    for (auto& bot : bots_) {
        bot.kill();
    }
}
