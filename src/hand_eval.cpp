#include "hand_eval.h"
#include <algorithm>

bool HandScore::operator>(const HandScore& o) const {
    if (rank != o.rank) return rank > o.rank;
    return kickers > o.kickers;
}

bool HandScore::operator==(const HandScore& o) const {
    return rank == o.rank && kickers == o.kickers;
}

static int rank_value(Rank r) {
    return static_cast<int>(r);
}

HandScore evaluate_five(const std::array<Card, 5>& cards) {
    // sort by rank descending
    std::array<int, 5> vals;
    std::array<int, 5> suits;
    for (int i = 0; i < 5; i++) {
        vals[i] = rank_value(cards[i].rank);
        suits[i] = static_cast<int>(cards[i].suit);
    }
    std::sort(vals.begin(), vals.end(), std::greater<int>());

    bool is_flush = (suits[0] == suits[1] && suits[1] == suits[2] &&
                     suits[2] == suits[3] && suits[3] == suits[4]);

    bool is_straight = false;
    int straight_high = 0;
    // normal straight
    if (vals[0] - vals[4] == 4 &&
        vals[0] != vals[1] && vals[1] != vals[2] &&
        vals[2] != vals[3] && vals[3] != vals[4]) {
        is_straight = true;
        straight_high = vals[0];
    }
    // wheel (A-2-3-4-5): ace=14, then 5,4,3,2
    if (vals[0] == 14 && vals[1] == 5 && vals[2] == 4 && vals[3] == 3 && vals[4] == 2) {
        is_straight = true;
        straight_high = 5; // 5-high straight
    }

    // count ranks
    int counts[15] = {};
    for (int v : vals) counts[v]++;

    int four_val = 0, three_val = 0;
    int pairs[2] = {}, pair_count = 0;
    for (int r = 14; r >= 2; r--) {
        if (counts[r] == 4) four_val = r;
        else if (counts[r] == 3) three_val = r;
        else if (counts[r] == 2 && pair_count < 2) pairs[pair_count++] = r;
    }

    HandScore hs;
    hs.kickers = {};

    if (is_straight && is_flush) {
        if (straight_high == 14) {
            hs.rank = HandRank::ROYAL_FLUSH;
            hs.kickers[0] = 14;
        } else {
            hs.rank = HandRank::STRAIGHT_FLUSH;
            hs.kickers[0] = straight_high;
        }
    } else if (four_val) {
        hs.rank = HandRank::FOUR_OF_A_KIND;
        hs.kickers[0] = four_val;
        int ki = 1;
        for (int v : vals) {
            if (v != four_val) { hs.kickers[ki++] = v; break; }
        }
    } else if (three_val && pair_count >= 1) {
        hs.rank = HandRank::FULL_HOUSE;
        hs.kickers[0] = three_val;
        hs.kickers[1] = pairs[0];
    } else if (is_flush) {
        hs.rank = HandRank::FLUSH;
        for (int i = 0; i < 5; i++) hs.kickers[i] = vals[i];
    } else if (is_straight) {
        hs.rank = HandRank::STRAIGHT;
        hs.kickers[0] = straight_high;
    } else if (three_val) {
        hs.rank = HandRank::THREE_OF_A_KIND;
        hs.kickers[0] = three_val;
        int ki = 1;
        for (int v : vals) {
            if (v != three_val) hs.kickers[ki++] = v;
        }
    } else if (pair_count == 2) {
        hs.rank = HandRank::TWO_PAIR;
        hs.kickers[0] = pairs[0];
        hs.kickers[1] = pairs[1];
        for (int v : vals) {
            if (v != pairs[0] && v != pairs[1]) { hs.kickers[2] = v; break; }
        }
    } else if (pair_count == 1) {
        hs.rank = HandRank::ONE_PAIR;
        hs.kickers[0] = pairs[0];
        int ki = 1;
        for (int v : vals) {
            if (v != pairs[0]) hs.kickers[ki++] = v;
        }
    } else {
        hs.rank = HandRank::HIGH_CARD;
        for (int i = 0; i < 5; i++) hs.kickers[i] = vals[i];
    }

    return hs;
}

HandScore evaluate_hand(const std::vector<Card>& cards) {
    int n = static_cast<int>(cards.size());
    HandScore best;
    best.rank = HandRank::HIGH_CARD;
    best.kickers = {};
    bool first = true;

    // try all C(n,5) combinations
    for (int a = 0; a < n - 4; a++) {
        for (int b = a + 1; b < n - 3; b++) {
            for (int c = b + 1; c < n - 2; c++) {
                for (int d = c + 1; d < n - 1; d++) {
                    for (int e = d + 1; e < n; e++) {
                        std::array<Card, 5> five = {cards[a], cards[b], cards[c], cards[d], cards[e]};
                        HandScore hs = evaluate_five(five);
                        if (first || hs > best) {
                            best = hs;
                            first = false;
                        }
                    }
                }
            }
        }
    }
    return best;
}
