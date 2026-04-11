#pragma once

#include "types.h"
#include "player.h"
#include "history.h"
#include <vector>
#include <array>
#include <random>

struct GameConfig {
    int num_players = 2;
    int starting_chips = 1000;
    std::array<int, 4> swap_cost_multipliers = {5, 15, 25, 50}; // preflop, flop, turn, river
};

class GameState {
public:
    explicit GameState(const GameConfig& config);

    // blind management
    int small_blind() const { return small_blind_; }
    int big_blind() const { return small_blind_ * 2; }
    int swap_cost(Street street) const;

    // dealer / blind positions
    int dealer_seat() const { return dealer_seat_; }
    int small_blind_seat() const;
    int big_blind_seat() const;

    // advance to next hand
    void advance_dealer();
    void check_eliminations();
    bool is_game_over() const;
    int winner_seat() const;       // -1 if tie
    bool is_tie() const;           // true if top stacks are equal
    std::vector<int> winners() const; // all seats with max chips (for ties)
    int revolutions() const { return revolutions_; }

    // accessors
    int num_players() const { return config_.num_players; }
    const GameConfig& config() const { return config_; }
    std::vector<Player>& players() { return players_; }
    const std::vector<Player>& players() const { return players_; }
    GameHistory& history() { return history_; }
    std::mt19937& rng() { return rng_; }
    int hand_number() const { return hand_number_; }
    void increment_hand() { hand_number_++; }

    // get next active seat in clockwise order
    int next_active_seat(int from) const;

    // count non-eliminated players
    int players_remaining() const;

    // smallest chip stack among non-eliminated players
    int min_active_stack() const;

private:
    GameConfig config_;
    std::vector<Player> players_;
    int dealer_seat_ = 0;
    int small_blind_ = 1;
    int hand_number_ = 0;
    int hands_at_current_blind_ = 0;
    int initial_player_count_ = 0;
    int revolutions_ = 0;
    static constexpr int MAX_REVOLUTIONS = 200;
    GameHistory history_;
    std::mt19937 rng_;
};
