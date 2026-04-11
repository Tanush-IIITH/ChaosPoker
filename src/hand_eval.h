#pragma once

#include "types.h"
#include <vector>
#include <array>

// A scored hand: category + kickers for comparison.
// Higher is better. Compared lexicographically.
struct HandScore {
    HandRank rank;
    std::array<int, 5> kickers; // tie-breaker values, highest first

    bool operator>(const HandScore& o) const;
    bool operator==(const HandScore& o) const;
    bool operator<(const HandScore& o) const { return o > *this; }
    bool operator>=(const HandScore& o) const { return !(*this < o); }
    bool operator<=(const HandScore& o) const { return !(*this > o); }
    bool operator!=(const HandScore& o) const { return !(*this == o); }
};

// Evaluate the best 5-card hand from any number of cards (typically 7: 2 hole + 5 community).
HandScore evaluate_hand(const std::vector<Card>& cards);

// Evaluate exactly 5 cards.
HandScore evaluate_five(const std::array<Card, 5>& cards);
