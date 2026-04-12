// smart_bot.cpp — Probabilistic Chaos Poker bot
// Strategy:
//   - Monte Carlo equity estimation with ~200 rollouts per decision
//   - Pot odds + implied odds for betting decisions
//   - Hand-strength-based swap decisions (swap weakest card if hand is weak)
//   - Vote based on current equity (YES if strong, NO if weak)
//   - Position-aware aggression (more aggressive in late position)
//   - Opponent tracking (fold frequency, aggression) for exploitation

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <random>
#include <chrono>
#include <cmath>
#include <numeric>
#include <cstring>

// Global variable to track which card index was last swapped
static int g_last_swap_index = 0;

// Global Hyperparameters for easy tuning
struct Hyperparameters {
    // --- Existing Aggression Stats ---
    double RAISE_BASE_WEIGHT = 1.0;
    double BLUFF_AGGR_THRESH = 0.25;
    double MIN_FOLD_RATE_BLUFF = 0.40;
    double RAISE_POT_WEIGHT = 1.0;
    double RAISE_STACK_WEIGHT = 1.0;
    double ALLIN_BASE_WEIGHT = 1.0;
    double ALLIN_POT_WEIGHT = 1.0;

    // --- Engine & Math ---
    int MC_ROLLOUTS_SWAP_PRE = 100;
    int MC_ROLLOUTS_SWAP_POST = 200;
    int MC_ROLLOUTS_VOTE = 150;
    int MC_ROLLOUTS_ACTION_DEFAULT = 200;
    int MC_ROLLOUTS_ACTION_RIVER = 300;

    double CHEN_DIVISOR = 20.0;
    double PREFLOP_BASE_EQ = 0.30;
    double PREFLOP_MULT_EQ = 0.55;
    double PREFLOP_MIN_EQ = 0.15;
    double PREFLOP_MAX_EQ = 0.95;

    double MULTIWAY_BASE_EXP = 0.8;
    double MULTIWAY_STEP_EXP = 0.2;

    // --- SWAP_PROMPT ---
    double SWAP_PRE_STAY_CHEN = 8.0;
    double SWAP_PRE_WEAK_CHEN = 3.0;
    int SWAP_PRE_COST_FRACTION = 5;
    double SWAP_POST_MAX_EQ = 0.25;
    int SWAP_POST_COST_FRACTION = 8;

    // --- VOTE_PROMPT ---
    double VOTE_YES_MIN_EQ = 0.55;
    double VOTE_YES_NOISE_MIN = 0.15;
    double VOTE_YES_NOISE_MAX = 0.30;
    double VOTE_NO_MAX_EQ = 0.35;
    double VOTE_NO_NOISE_MIN = 0.10;
    double VOTE_NO_NOISE_MAX = 0.20;
    int VOTE_MIN_POT_BB_MULT = 3;

    // --- ACTION_PROMPT (Aggressor) ---
    double ACT_VAL_MONSTER_BASE_FRAC = 0.5;
    double ACT_VAL_MONSTER_EQ_MULT = 2.0;
    double ACT_VAL_MONSTER_EQ = 0.75;
    double ACT_VAL_THIN_EQ = 0.50;
    double ACT_VAL_THIN_SIZE = 0.35;
    int ACT_VAL_THIN_FREQ_MAX = 2;
    double ACT_BLUFF_MAX_EQ = 0.35;
    int ACT_BLUFF_MIN_STREET = 2;
    int ACT_BLUFF_SAFE_STACK_MULT = 3;
    double ACT_BLUFF_NIT_MASSIVE = 0.60;
    int ACT_BLUFF_FREQ_MASSIVE = 2;
    int ACT_BLUFF_FREQ_STD = 4;
    double ACT_BLUFF_NIT_3WAY = 0.65;
    int ACT_BLUFF_FREQ_3WAY = 8;
    double ACT_BLUFF_SIZE = 0.60;

    // --- ACTION_PROMPT (Defender) ---
    double DEF_RERAISE_MONSTER_EQ = 0.80;
    double DEF_ALLIN_MONSTER_EQ = 0.90;
    double DEF_RERAISE_SIZE = 0.80;
    double DEF_EV_MARGIN = 0.05;
    double DEF_VAL_RAISE_EQ = 0.65;
    double DEF_VAL_RAISE_SIZE = 0.50;
    double DEF_SURVIVAL_EQ = 0.50;
    double DEF_IMPLIED_MARGIN = 0.03;
    int DEF_IMPLIED_STACK_FRACTION = 6;
    int DEF_BLUFF_RAISE_MULT = 3;

    // --- Opponent Profiling ---
    int PROF_MIN_HANDS = 5;
    double PROF_DEFAULT_FOLD = 0.30;
    double PROF_RAISE_POT_CAP = 5.0;
    double PROF_ALLIN_POT_CAP = 10.0;
    int PROF_MIN_ACTIONS = 5;
    double PROF_DEFAULT_AGG = 0.30;
} HP;

// Card types and utilities
enum Suit { SPADES = 0, HEARTS, DIAMONDS, CLUBS };
enum Rank { TWO = 2, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, TEN, JACK, QUEEN, KING, ACE };

// Cactus Kev bit-packed card representation
// Bits: [rank_flag(16)][suit_flag(4)][rank_idx(4)][prime(8)]
using Card = uint32_t;

static const int PRIMES[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41 };

static uint32_t make_card(int rank_idx, int suit) {
    uint32_t prime = PRIMES[rank_idx];
    uint32_t rank_val = (rank_idx << 8);
    uint32_t suit_flag = (0x1000 << suit);
    uint32_t rank_flag = (1 << (rank_idx + 16));
    return prime | rank_val | suit_flag | rank_flag;
}

static inline int get_rank(Card c) {
    return ((c >> 8) & 0xF) + 2;  // returns 2-14
}

static inline int get_suit(Card c) {
    uint32_t s = (c >> 12) & 0xF;
    if (s == 0x1) return SPADES;
    if (s == 0x2) return HEARTS;
    if (s == 0x4) return DIAMONDS;
    if (s == 0x8) return CLUBS;
    return SPADES;
}

static Card parse_card(const std::string& s) {
    int rank_idx = 0;
    switch (s[0]) {
        case '2': rank_idx = 0; break; case '3': rank_idx = 1; break;
        case '4': rank_idx = 2; break; case '5': rank_idx = 3; break;
        case '6': rank_idx = 4; break; case '7': rank_idx = 5; break;
        case '8': rank_idx = 6; break; case '9': rank_idx = 7; break;
        case 'T': rank_idx = 8; break; case 'J': rank_idx = 9; break;
        case 'Q': rank_idx = 10; break; case 'K': rank_idx = 11; break;
        case 'A': rank_idx = 12; break;
        default: rank_idx = 0;
    }
    int suit_idx = SPADES;
    switch (s[1]) {
        case 's': suit_idx = SPADES; break; case 'h': suit_idx = HEARTS; break;
        case 'd': suit_idx = DIAMONDS; break; case 'c': suit_idx = CLUBS; break;
        default: suit_idx = SPADES;
    }
    return make_card(rank_idx, suit_idx);
}

// Cactus Kev hand evaluator
// Uses precomputed lookup tables for O(1) 5-card evaluation.
// Returns 1 (Royal Flush) to 7462 (worst High Card) — lower is better.

#include "../ck_tables.h"

static int eval_5cards(uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4, uint32_t c5) {
    uint32_t q = (c1 | c2 | c3 | c4 | c5) >> 16;

    // Flush check: all five suit bits agree on one suit
    if (c1 & c2 & c3 & c4 & c5 & 0xF000) {
        return flushes[q];
    }

    // Straight or unique 5-card combo check
    short unique_val = unique5[q];
    if (unique_val != 0) {
        return unique_val;
    }

    // Pair / trips / quads: look up via prime product
    uint32_t q2 = (c1 & 0xFF) * (c2 & 0xFF) * (c3 & 0xFF) * (c4 & 0xFF) * (c5 & 0xFF);
    int low = 0, high = 4887;
    while (low <= high) {
        int mid = (low + high) / 2;
        if (prime_products[mid].prime_product < q2)      low  = mid + 1;
        else if (prime_products[mid].prime_product > q2) high = mid - 1;
        else return prime_products[mid].rank;
    }
    return 7462;
}

struct HandScore {
    int absolute_value; // higher is better (7463 - CK rank)
    bool operator>(const HandScore& o)  const { return absolute_value >  o.absolute_value; }
    bool operator==(const HandScore& o) const { return absolute_value == o.absolute_value; }
    bool operator>=(const HandScore& o) const { return absolute_value >= o.absolute_value; }
    bool operator<(const HandScore& o)  const { return absolute_value <  o.absolute_value; }
};

// Evaluate best 5-card hand out of 7 via all 21 combinations
static HandScore eval_7cards(const uint32_t* c) {
    int best_rank = 7463;
    for (int i = 0; i < 6; i++) {
        for (int j = i + 1; j < 7; j++) {
            uint32_t hand[5];
            int idx = 0;
            for (int k = 0; k < 7; k++) {
                if (k != i && k != j) hand[idx++] = c[k];
            }
            int r = eval_5cards(hand[0], hand[1], hand[2], hand[3], hand[4]);
            if (r < best_rank) best_rank = r;
        }
    }
    return {7463 - best_rank};
}

// Full deck generator + dead card removal

static std::vector<Card> build_remaining_deck(const std::vector<Card>& dead) {
    std::vector<Card> deck;
    deck.reserve(52);
    for (int s = 0; s < 4; s++)
        for (int r = 0; r < 13; r++) {
            Card c = make_card(r, s);
            bool is_dead = false;
            for (auto& d : dead) if (d == c) { is_dead = true; break; }
            if (!is_dead) deck.push_back(c);
        }
    return deck;
}

// Monte Carlo equity estimation

// Estimate hand equity against `num_opponents` opponents.
// `extra_dead` contains all cards confirmed removed from the deck this hand
// (discarded swap cards, old board cards before redraws, etc.)
// Uses `num_simulations` random rollouts. Returns win probability [0, 1].
static double estimate_equity(
    const Card& h0, const Card& h1,
    const std::vector<Card>& board,
    int num_opponents,
    int num_simulations,
    std::mt19937& rng,
    const std::vector<Card>& extra_dead = {})
{
    // Start with the caller-supplied dead set (which already contains h0, h1, board)
    // then add anything else we know is gone from the deck.
    std::vector<Card> dead = extra_dead;
    auto add_dead = [&](const Card& c) {
        for (auto& d : dead) if (d == c) return;
        dead.push_back(c);
    };
    add_dead(h0); add_dead(h1);
    for (auto& bc : board) add_dead(bc);
    auto remaining = build_remaining_deck(dead);

    int board_needed = 5 - (int)board.size();
    int cards_needed = board_needed + 2 * num_opponents;

    if ((int)remaining.size() < cards_needed) {
        // not enough cards — very rare, just return 0.5
        return 1.0 / (num_opponents + 1.0);
    }

    int wins = 0, ties = 0, total = 0;

    for (int sim = 0; sim < num_simulations; sim++) {
        // Fisher-Yates partial shuffle for only the cards we need
        auto deck = remaining;
        int n = (int)deck.size();
        int draw_count = std::min(cards_needed, n);
        for (int i = 0; i < draw_count; i++) {
            std::uniform_int_distribution<int> dist(i, n - 1);
            int j = dist(rng);
            std::swap(deck[i], deck[j]);
        }

        // fill community cards into a fixed-size array for eval_7cards
        uint32_t full_board[5];
        for (int i = 0; i < (int)board.size(); i++) full_board[i] = board[i];
        int idx = 0;
        for (int i = (int)board.size(); i < 5; i++)
            full_board[i] = deck[idx++];

        // evaluate our hand (hole cards + 5 community)
        uint32_t our_hand[7] = {
            full_board[0], full_board[1], full_board[2],
            full_board[3], full_board[4], h0, h1
        };
        HandScore our_score = eval_7cards(our_hand);

        // evaluate opponents
        bool we_win = true;
        bool any_tie = false;
        for (int opp = 0; opp < num_opponents; opp++) {
            uint32_t o0 = deck[idx++], o1 = deck[idx++];
            uint32_t opp_hand[7] = {
                full_board[0], full_board[1], full_board[2],
                full_board[3], full_board[4], o0, o1
            };
            HandScore opp_score = eval_7cards(opp_hand);
            if (opp_score > our_score) { we_win = false; break; }
            if (opp_score == our_score) any_tie = true;
        }

        if (we_win) {
            if (any_tie) ties++;
            else wins++;
        }
        total++;
    }

    return (wins + ties * 0.5) / total;
}

// Estimate the equity of a hand where we keep ONE hole card and draw a RANDOM replacement
// for the other. Used to decide which card to swap post-flop.
// `kept_card` is the card we are keeping; the missing hole card is dealt from the deck.
static double estimate_swap_equity(
    const Card& kept_card,
    const std::vector<Card>& board,
    int num_opponents,
    int num_simulations,
    std::mt19937& rng,
    const std::vector<Card>& extra_dead = {})
{
    std::vector<Card> dead = extra_dead;
    auto add_dead = [&](const Card& c) {
        for (auto& d : dead) if (d == c) return;
        dead.push_back(c);
    };
    add_dead(kept_card);
    for (auto& bc : board) add_dead(bc);
    auto remaining = build_remaining_deck(dead);

    int board_needed = 5 - (int)board.size();
    // Extra +1 for the missing hole card we will draw
    int cards_needed = board_needed + 1 + (2 * num_opponents);

    if ((int)remaining.size() < cards_needed) {
        return 1.0 / (num_opponents + 1.0);
    }

    int wins = 0, ties = 0, total = 0;

    for (int sim = 0; sim < num_simulations; sim++) {
        auto deck = remaining;
        int n = (int)deck.size();
        int draw_count = std::min(cards_needed, n);
        for (int i = 0; i < draw_count; i++) {
            std::uniform_int_distribution<int> dist(i, n - 1);
            int j = dist(rng);
            std::swap(deck[i], deck[j]);
        }

        // Fill community cards
        uint32_t full_board[5];
        for (int i = 0; i < (int)board.size(); i++) full_board[i] = board[i];
        int idx = 0;
        for (int i = (int)board.size(); i < 5; i++)
            full_board[i] = deck[idx++];

        // Deal one random card as our missing hole card
        uint32_t drawn_card = deck[idx++];

        uint32_t our_hand[7] = {
            full_board[0], full_board[1], full_board[2],
            full_board[3], full_board[4], kept_card, drawn_card
        };
        HandScore our_score = eval_7cards(our_hand);

        bool we_win = true;
        bool any_tie = false;
        for (int opp = 0; opp < num_opponents; opp++) {
            uint32_t o0 = deck[idx++], o1 = deck[idx++];
            uint32_t opp_hand[7] = {
                full_board[0], full_board[1], full_board[2],
                full_board[3], full_board[4], o0, o1
            };
            HandScore opp_score = eval_7cards(opp_hand);
            if (opp_score > our_score) { we_win = false; break; }
            if (opp_score == our_score) any_tie = true;
        }

        if (we_win) {
            if (any_tie) ties++;
            else wins++;
        }
        total++;
    }

    return (wins + ties * 0.5) / total;
}

// Pre-flop hand strength heuristic (Chen formula approximation)

static double preflop_strength(const Card& c0, const Card& c1) {
    int r0 = get_rank(c0), r1 = get_rank(c1);
    if (r0 < r1) std::swap(r0, r1);

    // base score from highest card
    double score;
    if (r0 == 14) score = 10;
    else if (r0 == 13) score = 8;
    else if (r0 == 12) score = 7;
    else if (r0 == 11) score = 6;
    else score = r0 / 2.0;

    // pair bonus
    if (r0 == r1) {
        score = std::max(score * 2.0, 5.0);
    }

    // suited bonus
    bool suited = (get_suit(c0) == get_suit(c1));
    if (suited) score += 2;

    // gap penalty
    int gap = r0 - r1 - 1;
    if (gap == 1) score -= 1;
    else if (gap == 2) score -= 2;
    else if (gap == 3) score -= 4;
    else if (gap >= 4) score -= 5;

    // straight potential bonus for close cards
    if (gap <= 1 && r0 <= 12) score += 1;

    return score;
}

// Game State Tracking

struct GameState {
    int num_players = 2;
    int my_seat = 0;
    int starting_chips = 1000;
    std::array<int, 4> swap_mults = {5, 15, 25, 50};
    int small_blind = 1;
    int big_blind = 2;

    // current hand state
    Card hole[2]{};
    std::vector<Card> board;
    std::vector<int> chips;  // all players' chips
    int dealer_seat = 0;
    int sb_seat = 0;
    int bb_seat = 0;
    int hand_num = 0;

    // active tracking
    std::vector<bool> folded;
    std::vector<bool> eliminated;
    int pot_estimate = 0;

    // street tracking  (0=preflop, 1=flop, 2=turn, 3=river)
    int current_street = 0;

    // This is fed into build_remaining_deck() so Monte Carlo never re-deals them.
    std::vector<Card> dead_cards;

    // opponent stats (simple tracking)
    std::vector<int> total_hands_seen;
    std::vector<int> total_folds;
    std::vector<int> total_raises;
    std::vector<int> total_calls;

    // Weighted aggression: each RAISE/ALLIN is weighted by fraction of stack committed.
    // agg_score accumulates weighted severity; action_count is the denominator.
    std::vector<double> agg_score;
    std::vector<int>    action_count;

    void init(int n) {
        num_players = n;
        chips.resize(n, starting_chips);
        folded.resize(n, false);
        eliminated.resize(n, false);
        total_hands_seen.resize(n, 0);
        total_folds.resize(n, 0);
        total_raises.resize(n, 0);
        total_calls.resize(n, 0);
        agg_score.resize(n, 0.0);
        action_count.resize(n, 0);
    }

    // Add card to dead set if not already present
    void mark_dead(const Card& c) {
        for (auto& d : dead_cards) if (d == c) return;
        dead_cards.push_back(c);
    }

    // Build the known dead card list for Monte Carlo:
    // Our hole cards + board + all observed discards
    std::vector<Card> mc_dead() const {
        std::vector<Card> dead = dead_cards;
        // Always include current hole cards and board (caller may not have added them yet)
        auto add = [&](const Card& c) {
            for (auto& d : dead) if (d == c) return;
            dead.push_back(c);
        };
        add(hole[0]); add(hole[1]);
        for (auto& bc : board) add(bc);
        return dead;
    }

    int count_active_opponents() const {
        int cnt = 0;
        for (int i = 0; i < num_players; i++) {
            if (i != my_seat && !folded[i] && !eliminated[i]) cnt++;
        }
        return cnt;
    }

    double opponent_fold_rate(int seat) const {
        if (total_hands_seen[seat] < HP.PROF_MIN_HANDS) return HP.PROF_DEFAULT_FOLD;
        return (double)total_folds[seat] / total_hands_seen[seat];
    }

    double avg_opponent_aggression() const {
        double total = 0;
        int cnt = 0;
        for (int i = 0; i < num_players; i++) {
            if (i == my_seat || eliminated[i]) continue;
            // Require at least PROF_MIN_ACTIONS observed actions before trusting the profile
            if (action_count[i] < HP.PROF_MIN_ACTIONS) continue;
            // Weighted aggression index: agg_score / action_count
            // A passive player (only folds/calls) => 0.0
            // A min-raiser (fraction ~0.05 per raise, raising 30% of the time) => ~0.32
            // A maniac shoving ALLIN twice in 10 actions => 5.0/10 = 0.5
            total += agg_score[i] / action_count[i];
            cnt++;
        }
        return cnt > 0 ? total / cnt : HP.PROF_DEFAULT_AGG;
    }
};

// Main bot loop

int main() {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    // Override hyperparameters via environment variables if present
    if (const char* env = std::getenv("OPT_BLUFF_THRESH"))     HP.BLUFF_AGGR_THRESH = std::stod(env);
    if (const char* env = std::getenv("OPT_MIN_FOLD_RATE"))    HP.MIN_FOLD_RATE_BLUFF = std::stod(env);
    if (const char* env = std::getenv("OPT_RAISE_BASE_WEIGHT")) HP.RAISE_BASE_WEIGHT = std::stod(env);
    if (const char* env = std::getenv("OPT_RAISE_POT_WEIGHT")) HP.RAISE_POT_WEIGHT = std::stod(env);
    if (const char* env = std::getenv("OPT_RAISE_STACK_WEIGHT")) HP.RAISE_STACK_WEIGHT = std::stod(env);
    if (const char* env = std::getenv("OPT_ALLIN_BASE_WEIGHT"))  HP.ALLIN_BASE_WEIGHT = std::stod(env);
    if (const char* env = std::getenv("OPT_ALLIN_POT_WEIGHT"))   HP.ALLIN_POT_WEIGHT = std::stod(env);
    if (const char* env = std::getenv("OPT_ACT_VAL_MONSTER_BASE_FRAC")) HP.ACT_VAL_MONSTER_BASE_FRAC = std::stod(env);
    if (const char* env = std::getenv("OPT_ACT_VAL_MONSTER_EQ_MULT")) HP.ACT_VAL_MONSTER_EQ_MULT = std::stod(env);

    std::mt19937 rng(std::chrono::high_resolution_clock::now().time_since_epoch().count());

    GameState gs;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "GAME_START") {
            int n, seat, chips, m0, m1, m2, m3;
            iss >> n >> seat >> chips >> m0 >> m1 >> m2 >> m3;
            gs.num_players = n;
            gs.my_seat = seat;
            gs.starting_chips = chips;
            gs.swap_mults = {m0, m1, m2, m3};
            gs.init(n);
        }

        else if (cmd == "HAND_START") {
            int hn, dealer, sb_s, bb_s, sb, bb;
            iss >> hn >> dealer >> sb_s >> bb_s >> sb >> bb;
            gs.hand_num = hn;
            gs.dealer_seat = dealer;
            gs.sb_seat = sb_s;
            gs.bb_seat = bb_s;
            gs.small_blind = sb;
            gs.big_blind = bb;
            gs.board.clear();
            gs.current_street = 0;
            gs.pot_estimate = 0;
            std::fill(gs.folded.begin(), gs.folded.end(), false);
            // mark eliminated players as folded too
            for (int i = 0; i < gs.num_players; i++) {
                if (gs.eliminated[i]) gs.folded[i] = true;
            }
        }

        else if (cmd == "CHIPS") {
            for (int i = 0; i < gs.num_players; i++) {
                iss >> gs.chips[i];
                if (gs.chips[i] == 0) gs.eliminated[i] = true;
            }
            for (int i = 0; i < gs.num_players; i++) {
                if (!gs.eliminated[i]) gs.total_hands_seen[i]++;
            }
        }

        else if (cmd == "DEAL_HOLE") {
            std::string c0, c1;
            iss >> c0 >> c1;
            gs.hole[0] = parse_card(c0);
            gs.hole[1] = parse_card(c1);
            gs.dead_cards.clear(); // fresh hand
            gs.mark_dead(gs.hole[0]);
            gs.mark_dead(gs.hole[1]);
        }

        else if (cmd == "DEAL_FLOP") {
            std::string c0, c1, c2;
            iss >> c0 >> c1 >> c2;
            gs.board.clear();
            Card fc0 = parse_card(c0), fc1 = parse_card(c1), fc2 = parse_card(c2);
            gs.board.push_back(fc0);
            gs.board.push_back(fc1);
            gs.board.push_back(fc2);
            gs.mark_dead(fc0); gs.mark_dead(fc1); gs.mark_dead(fc2);
            gs.current_street = 1;
        }

        else if (cmd == "DEAL_TURN") {
            std::string c;
            iss >> c;
            while (gs.board.size() > 3) gs.board.pop_back();
            Card tc = parse_card(c);
            gs.board.push_back(tc);
            gs.mark_dead(tc);
            gs.current_street = 2;
        }

        else if (cmd == "DEAL_RIVER") {
            std::string c;
            iss >> c;
            while (gs.board.size() > 4) gs.board.pop_back();
            Card rc = parse_card(c);
            gs.board.push_back(rc);
            gs.mark_dead(rc);
            gs.current_street = 3;
        }

        else if (cmd == "REDRAW_FLOP") {
            std::string c0, c1, c2;
            iss >> c0 >> c1 >> c2;
            gs.board.clear();
            Card rf0 = parse_card(c0), rf1 = parse_card(c1), rf2 = parse_card(c2);
            gs.board.push_back(rf0); gs.board.push_back(rf1); gs.board.push_back(rf2);
            gs.mark_dead(rf0); gs.mark_dead(rf1); gs.mark_dead(rf2);
        }

        else if (cmd == "REDRAW_TURN") {
            std::string c;
            iss >> c;
            while (gs.board.size() > 3) gs.board.pop_back();
            Card rt = parse_card(c);
            gs.board.push_back(rt);
            gs.mark_dead(rt);
        }

        else if (cmd == "REDRAW_RIVER") {
            std::string c;
            iss >> c;
            while (gs.board.size() > 4) gs.board.pop_back();
            Card rr = parse_card(c);
            gs.board.push_back(rr);
            gs.mark_dead(rr);
        }

        else if (cmd == "SWAP_RESULT") {
            std::string c;
            iss >> c;
            Card new_card = parse_card(c);
            gs.hole[g_last_swap_index] = new_card;
            gs.mark_dead(new_card);
        }

        else if (cmd == "ACTION") {
            int seat;
            std::string action;
            iss >> seat >> action;

            // Count every action (fold/call/raise/allin) as the denominator
            if (seat != gs.my_seat) gs.action_count[seat]++;

            if (action == "FOLD") {
                gs.folded[seat] = true;
                if (seat != gs.my_seat) gs.total_folds[seat]++;
            } else if (action == "CALL") {
                int amt; iss >> amt;
                gs.pot_estimate += amt;
                if (seat != gs.my_seat) gs.total_calls[seat]++;
            } else if (action == "RAISE") {
                int amt; iss >> amt;
                if (seat != gs.my_seat) {
                    gs.total_raises[seat]++;
                    
                    double cur_pot = std::max(1, gs.pot_estimate);
                    double pot_ratio = std::min(HP.PROF_RAISE_POT_CAP, (double)amt / cur_pot);

                    double stack = std::max(1, gs.chips[seat]);
                    double stack_fraction = std::min(1.0, (double)amt / stack);

                    gs.agg_score[seat] += HP.RAISE_BASE_WEIGHT + (pot_ratio * HP.RAISE_POT_WEIGHT) + (stack_fraction * HP.RAISE_STACK_WEIGHT);
                }
                gs.pot_estimate += amt; // updated AFTER calculations
            } else if (action == "ALLIN") {
                int amt; iss >> amt;
                if (seat != gs.my_seat) {
                    gs.total_raises[seat]++;

                    double cur_pot = std::max(1, gs.pot_estimate);
                    double risk_ratio = std::min(HP.PROF_ALLIN_POT_CAP, (double)amt / cur_pot);
                    
                    gs.agg_score[seat] += HP.ALLIN_BASE_WEIGHT + (risk_ratio * HP.ALLIN_POT_WEIGHT);
                }
                gs.pot_estimate += amt; // updated AFTER calculations
            }
            // CHECK doesn't change pot or aggression
        }

        else if (cmd == "SWAP_DONE") {
            // nothing to do
        }

        else if (cmd == "VOTE_RESULT") {
            // nothing to do, board updates via REDRAW if applicable
        }

        else if (cmd == "SHOWDOWN") {
            // informational — ignore
        }

        else if (cmd == "WINNER") {
            // informational — ignore
        }

        else if (cmd == "ELIMINATE") {
            int seat; iss >> seat;
            gs.eliminated[seat] = true;
        }

        else if (cmd == "GAME_OVER") {
            break;
        }

        else if (cmd == "SWAP_PROMPT") {
            int cost, my_chips;
            iss >> cost >> my_chips;

            int num_opp = gs.count_active_opponents();

            if (gs.current_street == 0) {
                // Pre-flop: use Chen heuristic
                double strength = preflop_strength(gs.hole[0], gs.hole[1]);

                if (strength >= HP.SWAP_PRE_STAY_CHEN || cost > my_chips) {
                    // strong hand or can't afford — keep
                    std::cout << "STAY" << std::endl;
                } else if (strength < HP.SWAP_PRE_WEAK_CHEN && cost <= my_chips / HP.SWAP_PRE_COST_FRACTION) {
                    // very weak hand, cheap swap — swap the lowest card
                    int swap_idx = (get_rank(gs.hole[0]) <= get_rank(gs.hole[1])) ? 0 : 1;
                    g_last_swap_index = swap_idx;
                    std::cout << "SWAP " << swap_idx << std::endl;
                } else {
                    std::cout << "STAY" << std::endl;
                }
            } else {
                // Post-flop: use Monte Carlo to evaluate current hand
                // then estimate what equity we'd expect from a random replacement
                double equity = estimate_equity(gs.hole[0], gs.hole[1], gs.board,
                                                std::max(1, num_opp), HP.MC_ROLLOUTS_SWAP_PRE, rng, gs.mc_dead());

                // Only swap if hand is weak AND cost is reasonable
                if (equity < HP.SWAP_POST_MAX_EQ && cost <= my_chips / HP.SWAP_POST_COST_FRACTION) {
                    // Pure Monte Carlo: simulate equity for keeping each hole card
                    // and drawing a random replacement for the other.
                    // Keeping Card 0 means we swap Card 1, and vice versa.
                    double eq_keep_0 = estimate_swap_equity(
                        gs.hole[0], gs.board, std::max(1, num_opp), HP.MC_ROLLOUTS_SWAP_POST, rng, gs.mc_dead());
                    double eq_keep_1 = estimate_swap_equity(
                        gs.hole[1], gs.board, std::max(1, num_opp), HP.MC_ROLLOUTS_SWAP_POST, rng, gs.mc_dead());

                    int swap_idx;
                    if (eq_keep_0 > eq_keep_1)       swap_idx = 1; // keeping 0 is better → swap 1
                    else if (eq_keep_1 > eq_keep_0)  swap_idx = 0; // keeping 1 is better → swap 0
                    else swap_idx = (get_rank(gs.hole[0]) <= get_rank(gs.hole[1])) ? 0 : 1; // tie: discard lower rank

                    g_last_swap_index = swap_idx;
                    std::cout << "SWAP " << swap_idx << std::endl;
                } else {
                    std::cout << "STAY" << std::endl;
                }
            }
        }

        else if (cmd == "VOTE_PROMPT") {
            int my_chips;
            iss >> my_chips;

            int num_opp = gs.count_active_opponents();

            if (gs.board.empty()) {
                std::cout << "VOTE YES 0" << std::endl;
            } else {
                double equity = estimate_equity(gs.hole[0], gs.hole[1], gs.board,
                                                std::max(1, num_opp), HP.MC_ROLLOUTS_VOTE, rng, gs.mc_dead());

                // 1. Calculate Expected Values
                double current_val = equity * gs.pot_estimate;

                double chen = preflop_strength(gs.hole[0], gs.hole[1]);
                double pf_eq = HP.PREFLOP_BASE_EQ + (chen / HP.CHEN_DIVISOR) * HP.PREFLOP_MULT_EQ;
                pf_eq = std::max(HP.PREFLOP_MIN_EQ, std::min(pf_eq, HP.PREFLOP_MAX_EQ));
                if (num_opp > 1) {
                    pf_eq = std::pow(pf_eq, HP.MULTIWAY_BASE_EXP + HP.MULTIWAY_STEP_EXP * num_opp);
                }
                double redraw_val = pf_eq * gs.pot_estimate;

                // 2. The Expected Value Gap (Delta EV)
                double ev_gap = current_val - redraw_val;

                // 3. Mixed Strategy Randomizers (Prevents exploitation)
                std::uniform_real_distribution<double> yes_noise(HP.VOTE_YES_NOISE_MIN, HP.VOTE_YES_NOISE_MAX);
                std::uniform_real_distribution<double> no_noise(HP.VOTE_NO_NOISE_MIN, HP.VOTE_NO_NOISE_MAX);

                if (equity >= HP.VOTE_YES_MIN_EQ && ev_gap > 0) {
                    // We want to KEEP the board.
                    int wager = (int)(ev_gap * yes_noise(rng));

                    // Sanity check: Don't spend chips if the pot is virtually empty
                    if (gs.pot_estimate < gs.big_blind * HP.VOTE_MIN_POT_BB_MULT) wager = 0;

                    wager = std::max(0, std::min(wager, my_chips));
                    std::cout << "VOTE YES " << wager << std::endl;

                } else if (equity < HP.VOTE_NO_MAX_EQ && ev_gap < 0) {
                    // We want to DESTROY the board.
                    int wager = (int)(std::abs(ev_gap) * no_noise(rng));

                    if (gs.pot_estimate < gs.big_blind * HP.VOTE_MIN_POT_BB_MULT) wager = 0;

                    wager = std::max(0, std::min(wager, my_chips));
                    std::cout << "VOTE NO " << wager << std::endl;

                } else {
                    std::cout << "VOTE YES 0" << std::endl;
                }
            }
        }

        else if (cmd == "ACTION_PROMPT") {
            int my_chips, current_bet, my_bet, min_raise, pot;
            iss >> my_chips >> current_bet >> my_bet >> min_raise >> pot;

            int to_call = current_bet - my_bet;
            int num_opp = gs.count_active_opponents();

            double equity;

            if (gs.board.empty()) {
                // Pre-flop: use heuristic + light simulation
                double chen = preflop_strength(gs.hole[0], gs.hole[1]);
                // Map Chen score to rough equity
                // Top hands (AA, KK, AKs) => Chen ~20  => equity ~0.85
                // Medium (JTs, 88) => Chen ~8 => equity ~0.55
                // Weak (72o) => Chen ~-1 => equity ~0.30
                equity = HP.PREFLOP_BASE_EQ + (chen / HP.CHEN_DIVISOR) * HP.PREFLOP_MULT_EQ;
                equity = std::max(HP.PREFLOP_MIN_EQ, std::min(equity, HP.PREFLOP_MAX_EQ));

                // Adjust for number of opponents (more opponents = lower equity)
                if (num_opp > 1) {
                    equity = std::pow(equity, HP.MULTIWAY_BASE_EXP + HP.MULTIWAY_STEP_EXP * num_opp);
                }
            } else {
                // Post-flop: Monte Carlo
                int sim_count = (gs.current_street == 3) ? HP.MC_ROLLOUTS_ACTION_RIVER : HP.MC_ROLLOUTS_ACTION_DEFAULT;
                equity = estimate_equity(gs.hole[0], gs.hole[1], gs.board,
                                         std::max(1, num_opp), sim_count, rng, gs.mc_dead());
            }

            // Pot odds
            double pot_odds = (to_call > 0) ? (double)to_call / (pot + to_call) : 0.0;

            // ---- Decision tree ----

            if (to_call <= 0) {
                // No bet to call — can check or bet
                if (equity >= HP.ACT_VAL_MONSTER_EQ) {
                    // Strong: bet/raise for value
                    int raise_amt = min_raise;
                    // Size bet based on equity and pot
                    double bet_frac = HP.ACT_VAL_MONSTER_BASE_FRAC + (equity - HP.ACT_VAL_MONSTER_EQ) * HP.ACT_VAL_MONSTER_EQ_MULT;
                    int desired = (int)(pot * bet_frac);
                    raise_amt = std::max(min_raise, desired);
                    int max_raise = my_chips + my_bet;
                    raise_amt = std::min(raise_amt, max_raise);

                    if (raise_amt > current_bet && min_raise <= max_raise) {
                        std::cout << "RAISE " << raise_amt << std::endl;
                    } else {
                        std::cout << "CHECK" << std::endl;
                    }
                } else if (equity >= HP.ACT_VAL_THIN_EQ) {
                    // Medium-strong: small bet or check
                    // Bet ~ACT_VAL_THIN_SIZE pot sometimes for thin value
                    if (std::uniform_int_distribution<int>(0, HP.ACT_VAL_THIN_FREQ_MAX)(rng) == 0) {
                        int raise_amt = std::max(min_raise, (int)(pot * HP.ACT_VAL_THIN_SIZE));
                        int max_raise = my_chips + my_bet;
                        raise_amt = std::min(raise_amt, max_raise);
                        if (raise_amt > current_bet && min_raise <= max_raise) {
                            std::cout << "RAISE " << raise_amt << std::endl;
                        } else {
                            std::cout << "CHECK" << std::endl;
                        }
                    } else {
                        std::cout << "CHECK" << std::endl;
                    }
                } else if (equity >= HP.ACT_BLUFF_MAX_EQ) {
                    // Marginal: check
                    std::cout << "CHECK" << std::endl;
                } else {
                    // Weak hand — check for free card, or execute a heads-up-scaled bluff
                    // Compute active opponent count and find the hardest opponent to bluff
                    double min_active_fold_rate = 1.0;
                    int active_opp_count = 0;
                    for (int i = 0; i < gs.num_players; i++) {
                        if (i != gs.my_seat && !gs.folded[i] && !gs.eliminated[i]) {
                            double opp_fold = gs.opponent_fold_rate(i);
                            if (opp_fold < min_active_fold_rate) min_active_fold_rate = opp_fold;
                            active_opp_count++;
                        }
                    }
                    if (active_opp_count == 0) min_active_fold_rate = HP.PROF_DEFAULT_FOLD;

                    bool should_bluff = false;
                    double avg_agg = gs.avg_opponent_aggression();
                    if (avg_agg < HP.BLUFF_AGGR_THRESH && gs.current_street >= HP.ACT_BLUFF_MIN_STREET && my_chips > current_bet * HP.ACT_BLUFF_SAFE_STACK_MULT) {
                        if (active_opp_count == 1) {
                            // Heads-up: bluff aggressively if opponent is a Nit
                            if (min_active_fold_rate > HP.MIN_FOLD_RATE_BLUFF) {
                                int bluff_chance = (min_active_fold_rate > HP.ACT_BLUFF_NIT_MASSIVE) ? HP.ACT_BLUFF_FREQ_MASSIVE : HP.ACT_BLUFF_FREQ_STD;
                                if (std::uniform_int_distribution<int>(0, bluff_chance)(rng) == 0)
                                    should_bluff = true;
                            }
                        } else if (active_opp_count == 2) {
                            // 3-way: both must be solid Nits, and very low frequency
                            if (min_active_fold_rate > HP.ACT_BLUFF_NIT_3WAY &&
                                std::uniform_int_distribution<int>(0, HP.ACT_BLUFF_FREQ_3WAY)(rng) == 0)
                                should_bluff = true;
                        }
                        // 3+ opponents: never bluff
                    }

                    if (should_bluff) {
                        int raise_amt = std::max(min_raise, (int)(pot * HP.ACT_BLUFF_SIZE));
                        int max_raise = my_chips + my_bet;
                        raise_amt = std::min(raise_amt, max_raise);
                        if (raise_amt > current_bet && min_raise <= max_raise) {
                            std::cout << "RAISE " << raise_amt << std::endl;
                        } else {
                            std::cout << "CHECK" << std::endl;
                        }
                    } else {
                        std::cout << "CHECK" << std::endl;
                    }
                }
            } else {
                // Facing a bet — need to call, raise, or fold
                if (equity >= HP.DEF_RERAISE_MONSTER_EQ) {
                    // Monster hand: raise big
                    if (equity >= HP.DEF_ALLIN_MONSTER_EQ) {
                        std::cout << "ALLIN" << std::endl;
                    } else {
                        int raise_amt = std::max(min_raise, (int)(pot * HP.DEF_RERAISE_SIZE + current_bet));
                        int max_raise = my_chips + my_bet;
                        raise_amt = std::min(raise_amt, max_raise);
                        if (min_raise <= max_raise && raise_amt > current_bet) {
                            std::cout << "RAISE " << raise_amt << std::endl;
                        } else if (to_call <= my_chips) {
                            std::cout << "CALL" << std::endl;
                        } else {
                            std::cout << "ALLIN" << std::endl;
                        }
                    }
                } else if (equity > pot_odds + HP.DEF_EV_MARGIN) {
                    // Positive EV call
                    if (equity >= HP.DEF_VAL_RAISE_EQ && min_raise <= my_chips + my_bet) {
                        // Strong enough to raise
                        int raise_amt = std::max(min_raise, current_bet + (int)(pot * HP.DEF_VAL_RAISE_SIZE));
                        int max_raise = my_chips + my_bet;
                        raise_amt = std::min(raise_amt, max_raise);
                        if (raise_amt > current_bet) {
                            std::cout << "RAISE " << raise_amt << std::endl;
                        } else if (to_call <= my_chips) {
                            std::cout << "CALL" << std::endl;
                        } else {
                            std::cout << "ALLIN" << std::endl;
                        }
                    } else if (to_call <= my_chips) {
                        std::cout << "CALL" << std::endl;
                    } else {
                        // Need to go all-in to call — only if strong
                        if (equity >= HP.DEF_SURVIVAL_EQ) {
                            std::cout << "ALLIN" << std::endl;
                        } else {
                            std::cout << "FOLD" << std::endl;
                        }
                    }
                } else if (equity > pot_odds - HP.DEF_IMPLIED_MARGIN) {
                    // Borderline — call if the cost is small relative to stack
                    if (to_call <= my_chips / HP.DEF_IMPLIED_STACK_FRACTION && to_call <= my_chips) {
                        std::cout << "CALL" << std::endl;
                    } else {
                        std::cout << "FOLD" << std::endl;
                    }
                } else {
                    // Negative EV facing a bet — apply heads-up-scaled bluff-raise
                    double min_active_fold_rate = 1.0;
                    int active_opp_count = 0;
                    for (int i = 0; i < gs.num_players; i++) {
                        if (i != gs.my_seat && !gs.folded[i] && !gs.eliminated[i]) {
                            double opp_fold = gs.opponent_fold_rate(i);
                            if (opp_fold < min_active_fold_rate) min_active_fold_rate = opp_fold;
                            active_opp_count++;
                        }
                    }
                    if (active_opp_count == 0) min_active_fold_rate = HP.PROF_DEFAULT_FOLD;

                    bool should_bluff = false;
                    double avg_agg = gs.avg_opponent_aggression();

                    if (avg_agg < HP.BLUFF_AGGR_THRESH && gs.current_street >= HP.ACT_BLUFF_MIN_STREET && my_chips > current_bet * HP.ACT_BLUFF_SAFE_STACK_MULT) {
                        if (active_opp_count == 1) {
                            if (min_active_fold_rate > HP.MIN_FOLD_RATE_BLUFF) {
                                int bluff_chance = (min_active_fold_rate > HP.ACT_BLUFF_NIT_MASSIVE) ? HP.ACT_BLUFF_FREQ_MASSIVE : HP.ACT_BLUFF_FREQ_STD;
                                if (std::uniform_int_distribution<int>(0, bluff_chance)(rng) == 0)
                                    should_bluff = true;
                            }
                        } else if (active_opp_count == 2) {
                            if (min_active_fold_rate > HP.ACT_BLUFF_NIT_3WAY &&
                                std::uniform_int_distribution<int>(0, HP.ACT_BLUFF_FREQ_3WAY)(rng) == 0)
                                should_bluff = true;
                        }
                    }

                    if (should_bluff) {
                        int raise_amt = std::max(min_raise, current_bet * HP.DEF_BLUFF_RAISE_MULT);
                        int max_raise = my_chips + my_bet;
                        raise_amt = std::min(raise_amt, max_raise);
                        if (raise_amt > current_bet && min_raise <= max_raise) {
                            std::cout << "RAISE " << raise_amt << std::endl;
                        } else {
                            std::cout << "FOLD" << std::endl;
                        }
                    } else {
                        std::cout << "FOLD" << std::endl;
                    }
                }
            }
        }
    }

    return 0;
}


