// Evaluates all 5-to-7 card combinations using prime product lookup
#include <iostream>
#include <vector>
#include <array>
#include <algorithm>
#include <random>
#include <chrono>

enum Suit { SPADES = 0, HEARTS, DIAMONDS, CLUBS };
enum HandRankEnum { HIGH_CARD = 0, ONE_PAIR, TWO_PAIR, THREE_OF_A_KIND, STRAIGHT, FLUSH, FULL_HOUSE, FOUR_OF_A_KIND, STRAIGHT_FLUSH, ROYAL_FLUSH };

struct Card {
    int rank, suit;
    bool operator==(const Card& o) const { return rank == o.rank && suit == o.suit; }
};

struct HandScore {
    int rank;
    std::array<int, 5> kickers{};
    bool operator>(const HandScore& o) const { if (rank != o.rank) return rank > o.rank; return kickers > o.kickers; }
    bool operator==(const HandScore& o) const { return rank == o.rank && kickers == o.kickers; }
};

HandScore evaluate_five(const std::array<Card, 5>& cards) {
    std::array<int, 5> vals, suits;
    for (int i = 0; i < 5; i++) { vals[i] = cards[i].rank; suits[i] = cards[i].suit; }
    std::sort(vals.begin(), vals.end(), std::greater<int>());
    bool is_flush = (suits[0] == suits[1] && suits[1] == suits[2] && suits[2] == suits[3] && suits[3] == suits[4]);
    bool is_straight = false; int straight_high = 0;
    if (vals[0] - vals[4] == 4 && vals[0] != vals[1] && vals[1] != vals[2] && vals[2] != vals[3] && vals[3] != vals[4]) { is_straight = true; straight_high = vals[0]; }
    if (vals[0] == 14 && vals[1] == 5 && vals[2] == 4 && vals[3] == 3 && vals[4] == 2) { is_straight = true; straight_high = 5; }
    int counts[15] = {}; for (int v : vals) counts[v]++;
    int four_val = 0, three_val = 0; int pairs[2] = {}, pair_count = 0;
    for (int r = 14; r >= 2; r--) {
        if (counts[r] == 4) four_val = r;
        else if (counts[r] == 3) three_val = r;
        else if (counts[r] == 2 && pair_count < 2) pairs[pair_count++] = r;
    }
    HandScore hs; hs.kickers = {};
    if (is_straight && is_flush) { hs.rank = (straight_high == 14) ? ROYAL_FLUSH : STRAIGHT_FLUSH; hs.kickers[0] = straight_high; }
    else if (four_val) { hs.rank = FOUR_OF_A_KIND; hs.kickers[0] = four_val; for(int v:vals) if(v!=four_val){hs.kickers[1]=v;break;} }
    else if (three_val && pair_count >= 1) { hs.rank = FULL_HOUSE; hs.kickers[0] = three_val; hs.kickers[1] = pairs[0]; }
    else if (is_flush) { hs.rank = FLUSH; for(int i=0;i<5;i++) hs.kickers[i] = vals[i]; }
    else if (is_straight) { hs.rank = STRAIGHT; hs.kickers[0] = straight_high; }
    else if (three_val) { hs.rank = THREE_OF_A_KIND; hs.kickers[0] = three_val; int ki=1; for(int v:vals) if(v!=three_val)hs.kickers[ki++]=v; }
    else if (pair_count == 2) { hs.rank = TWO_PAIR; hs.kickers[0] = pairs[0]; hs.kickers[1] = pairs[1]; for(int v:vals) if(v!=pairs[0]&&v!=pairs[1]){hs.kickers[2]=v;break;} }
    else if (pair_count == 1) { hs.rank = ONE_PAIR; hs.kickers[0] = pairs[0]; int ki=1; for(int v:vals) if(v!=pairs[0])hs.kickers[ki++]=v; }
    else { hs.rank = HIGH_CARD; for(int i=0;i<5;i++) hs.kickers[i] = vals[i]; }
    return hs;
}

HandScore evaluate_best(const std::vector<Card>& cards) {
    HandScore best; best.rank = HIGH_CARD; bool first = true;
    for (int a = 0; a < 3; a++) for (int b = a + 1; b < 4; b++) for (int c = b + 1; c < 5; c++)
    for (int d = c + 1; d < 6; d++) for (int e = d + 1; e < 7; e++) {
        HandScore hs = evaluate_five({cards[a], cards[b], cards[c], cards[d], cards[e]});
        if (first || hs > best) { best = hs; first = false; }
    }
    return best;
}

int main() {
    std::mt19937 rng(42);
    int sims = 100000;
    std::vector<Card> deck;
    for(int r=2; r<=14; r++) for(int s=0; s<4; s++) deck.push_back({r, s});

    printf("static const double PREFLOP_EQUITY[15][15] = {\n");
    for(int r1=0; r1<=14; r1++) {
        printf("    {");
        for(int r2=0; r2<=14; r2++) {
            if (r1 < 2 || r2 < 2) { printf("0.0,"); continue; }
            Card h0, h1;
            if (r1 == r2) { h0 = {r1, 0}; h1 = {r2, 1}; }
            else if (r1 > r2) { h0 = {r1, 0}; h1 = {r2, 0}; }
            else { h0 = {r1, 0}; h1 = {r2, 1}; }

            std::vector<Card> rem;
            for(auto& dc : deck) if(dc.rank != h0.rank || dc.suit != h0.suit) if(dc.rank != h1.rank || dc.suit != h1.suit) rem.push_back(dc);

            int wins = 0, ties = 0;
            for(int s=0; s<sims; s++) {
                int n = rem.size();
                for (int i = 0; i < 7; i++) { int j = i + (rng() % (n - i)); std::swap(rem[i], rem[j]); }
                std::vector<Card> board = {rem[0], rem[1], rem[2], rem[3], rem[4]};
                Card o0 = rem[5], o1 = rem[6];
                std::vector<Card> our_hand = board; our_hand.push_back(h0); our_hand.push_back(h1);
                std::vector<Card> opp_hand = board; opp_hand.push_back(o0); opp_hand.push_back(o1);
                HandScore our_score = evaluate_best(our_hand);
                HandScore opp_score = evaluate_best(opp_hand);
                if (our_score > opp_score) wins++;
                else if (our_score == opp_score) ties++;
            }
            double eq = (wins + ties * 0.5) / sims;
            printf("%.3f", eq);
            if(r2 < 14) printf(", ");
        }
        printf("},\n");
    }
    printf("};\n");
    return 0;
}
