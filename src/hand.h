#pragma once

#include "types.h"
#include "deck.h"
#include "game_state.h"
#include "hand_eval.h"
#include <vector>
#include <functional>

// IO callback interface — the engine calls these to communicate with bots.
// For now these are stubs; the harness will provide real implementations.
struct BotIO {
    // send a message to a specific player
    std::function<void(int seat, const std::string& msg)> send;
    // send a message to all players
    std::function<void(const std::string& msg)> broadcast;
    // read a response from a player (returns empty string on timeout/error)
    std::function<std::string(int seat)> recv;
};

class Hand {
public:
    Hand(GameState& state, BotIO& io);

    // run the full hand, returns true if the game should continue
    bool run();

private:
    void deal_hole_cards();
    void deal_community(Street street);

    void swap_phase(Street street);
    void vote_phase(Street street);
    void betting_round(Street street);
    void showdown();

    // post blinds, returns false if not enough players
    bool post_blinds();

    // helper: count active (non-folded, non-eliminated) players
    int count_active() const;
    // helper: count players who can still act (active + not all-in)
    int count_can_act() const;
    // helper: award pot to last remaining player if all others folded
    bool check_single_winner();

    // resolve side pots and award winnings
    void resolve_pots();

    // fold a player due to timeout or bad IO
    void force_fold(int seat);

    GameState& state_;
    BotIO& io_;
    Deck deck_;
    std::vector<Card> community_cards_;
    int pot_ = 0;

    // per-player bet tracking for current betting round
    std::vector<int> round_bets_;
    // total chips put into pot by each player this hand
    std::vector<int> total_invested_;

    int current_bet_ = 0;
    int last_raise_size_ = 0;
    Street current_street_ = Street::PREFLOP;

    // tracks players force-folded (IO error) in the current phase
    std::vector<int> phase_force_folds_;
};
