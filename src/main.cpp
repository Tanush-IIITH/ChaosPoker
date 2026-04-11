#include "game_state.h"
#include "hand.h"
#include "harness.h"
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " [--history] <starting_chips>"
              << " <preflop_swap_mult> <flop_swap_mult> <turn_swap_mult> <river_swap_mult>"
              << " <bot1_cmd> <bot2_cmd> [bot3_cmd ...]"
              << std::endl;
}

int main(int argc, char* argv[]) {
    bool print_history = false;

    int arg_offset = 1;
    if (argc > 1 && std::strcmp(argv[1], "--history") == 0) {
        print_history = true;
        arg_offset = 2;
    }

    // need at least 5 config args + 2 bot commands = 7 args after offset
    if (argc - arg_offset < 7) {
        print_usage(argv[0]);
        return 1;
    }

    int starting_chips = std::atoi(argv[arg_offset]);
    int preflop_mult   = std::atoi(argv[arg_offset + 1]);
    int flop_mult      = std::atoi(argv[arg_offset + 2]);
    int turn_mult      = std::atoi(argv[arg_offset + 3]);
    int river_mult     = std::atoi(argv[arg_offset + 4]);

    // remaining args are bot commands
    std::vector<std::string> bot_commands;
    for (int i = arg_offset + 5; i < argc; i++) {
        bot_commands.push_back(argv[i]);
    }

    int num_players = static_cast<int>(bot_commands.size());
    if (num_players < 2) {
        std::cerr << "Need at least 2 bot commands\n";
        return 1;
    }

    GameConfig config;
    config.num_players = num_players;
    config.starting_chips = starting_chips;
    config.swap_cost_multipliers[0] = preflop_mult;
    config.swap_cost_multipliers[1] = flop_mult;
    config.swap_cost_multipliers[2] = turn_mult;
    config.swap_cost_multipliers[3] = river_mult;

    GameState state(config);

    // launch bot processes
    Harness harness(bot_commands);
    BotIO io = harness.get_io();

    // send GAME_START to each bot with their seat number
    harness.send_game_start(num_players, starting_chips,
                            preflop_mult, flop_mult, turn_mult, river_mult);

    // main game loop
    while (!state.is_game_over()) {
        state.check_eliminations();
        if (state.is_game_over()) break;

        Hand hand(state, io);
        hand.run();

        std::vector<bool> was_eliminated(num_players);
        for (int i = 0; i < num_players; i++) {
            was_eliminated[i] = state.players()[i].eliminated;
        }

        state.check_eliminations();

        for (int i = 0; i < num_players; i++) {
            if (!was_eliminated[i] && state.players()[i].eliminated) {
                io.broadcast("ELIMINATE " + std::to_string(i));
            }
        }

        if (state.is_game_over()) break;
        state.advance_dealer();
    }

    if (state.is_tie()) {
        auto w = state.winners();
        std::ostringstream oss;
        oss << "GAME_OVER TIE";
        for (int seat : w) oss << " " << seat;
        io.broadcast(oss.str());
    } else {
        io.broadcast("GAME_OVER " + std::to_string(state.winner_seat()));
    }

    if (print_history) {
        std::cerr << state.history().pretty_print();
    }

    harness.shutdown();
    return 0;
}
