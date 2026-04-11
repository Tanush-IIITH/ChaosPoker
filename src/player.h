#pragma once

#include "types.h"
#include <array>

struct Player {
    int seat = -1;
    int chips = 0;
    std::array<Card, 2> hole_cards{};
    bool folded = false;
    bool all_in = false;
    bool eliminated = false;

    bool is_active() const { return !folded && !eliminated; }
    bool can_act() const { return is_active() && !all_in; }

    void reset_for_hand() {
        folded = false;
        all_in = false;
        hole_cards = {};
    }
};
