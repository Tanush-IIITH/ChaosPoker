#include "game_state.h"
#include <chrono>
#include <algorithm>

GameState::GameState(const GameConfig& config) : config_(config) {
    players_.resize(config.num_players);
    for (int i = 0; i < config.num_players; i++) {
        players_[i].seat = i;
        players_[i].chips = config.starting_chips;
    }
    initial_player_count_ = config.num_players;
    auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    rng_.seed(static_cast<unsigned>(seed));
}

int GameState::swap_cost(Street street) const {
    return config_.swap_cost_multipliers[static_cast<int>(street)] * small_blind_;
}

int GameState::next_active_seat(int from) const {
    int n = config_.num_players;
    for (int i = 1; i <= n; i++) {
        int seat = (from + i) % n;
        if (!players_[seat].eliminated) {
            return seat;
        }
    }
    return from;
}

int GameState::small_blind_seat() const {
    if (players_remaining() == 2) {
        return dealer_seat_;
    }
    return next_active_seat(dealer_seat_);
}

int GameState::big_blind_seat() const {
    if (players_remaining() == 2) {
        return next_active_seat(dealer_seat_);
    }
    return next_active_seat(next_active_seat(dealer_seat_));
}

void GameState::advance_dealer() {
    dealer_seat_ = next_active_seat(dealer_seat_);
    hands_at_current_blind_++;

    // blinds increase after each full revolution
    if (hands_at_current_blind_ >= players_remaining()) {
        int min_stack = min_active_stack();
        int new_sb = std::max(small_blind_,
                              std::min(2 * small_blind_, (min_stack + 2) / 3));
        small_blind_ = new_sb;
        hands_at_current_blind_ = 0;
        revolutions_++;
    }
}

void GameState::check_eliminations() {
    for (auto& p : players_) {
        if (!p.eliminated && p.chips <= 0) {
            p.eliminated = true;
            p.chips = 0;
        }
    }
}

bool GameState::is_game_over() const {
    return players_remaining() <= 1 || revolutions_ >= MAX_REVOLUTIONS;
}

int GameState::winner_seat() const {
    auto w = winners();
    if (w.size() == 1) return w[0];
    return -1; // tie
}

bool GameState::is_tie() const {
    return winners().size() > 1;
}

std::vector<int> GameState::winners() const {
    int max_chips = 0;
    for (const auto& p : players_) {
        if (!p.eliminated && p.chips > max_chips) {
            max_chips = p.chips;
        }
    }
    std::vector<int> result;
    for (const auto& p : players_) {
        if (!p.eliminated && p.chips == max_chips) {
            result.push_back(p.seat);
        }
    }
    return result;
}

int GameState::players_remaining() const {
    int count = 0;
    for (const auto& p : players_) {
        if (!p.eliminated) count++;
    }
    return count;
}

int GameState::min_active_stack() const {
    int min_stack = INT32_MAX;
    for (const auto& p : players_) {
        if (!p.eliminated && p.chips < min_stack) {
            min_stack = p.chips;
        }
    }
    return min_stack == INT32_MAX ? 0 : min_stack;
}
