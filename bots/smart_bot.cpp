
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <algorithm>
#include <random>
#include <chrono>
#include <cmath>
#include <numeric>
#include <cstring>

// Timing utilities for strict 10ms decision budget
using Clock     = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
static inline TimePoint time_now() { return Clock::now(); }
static inline double elapsed_ms(TimePoint t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static int g_last_swap_index = 0;

struct Hyperparameters {

    double RAISE_BASE_WEIGHT = 1.0;
    double BLUFF_AGGR_THRESH = 0.25;
    double MIN_FOLD_RATE_BLUFF = 0.40;
    double RAISE_POT_WEIGHT = 1.0;
    double RAISE_STACK_WEIGHT = 1.0;
    double ALLIN_BASE_WEIGHT = 1.0;
    double ALLIN_POT_WEIGHT = 1.0;

    int MC_ROLLOUTS_SWAP_PRE = 100;
    int MC_ROLLOUTS_SWAP_POST = 500;
    int MC_ROLLOUTS_VOTE = 500;
    int MC_ROLLOUTS_ACTION_DEFAULT = 1000;
    int MC_ROLLOUTS_ACTION_RIVER = 2000;

    double CHEN_DIVISOR = 20.0;
    double PREFLOP_BASE_EQ = 0.30;
    double PREFLOP_MULT_EQ = 0.55;
    double PREFLOP_MIN_EQ = 0.15;
    double PREFLOP_MAX_EQ = 0.95;

    double MULTIWAY_BASE_EXP = 0.8;
    double MULTIWAY_STEP_EXP = 0.2;

    double SWAP_PRE_STAY_EQ = 0.65;
    double SWAP_PRE_WEAK_EQ = 0.40;
    int SWAP_PRE_COST_FRACTION = 5;
    double SWAP_POST_MAX_EQ = 0.25;
    int SWAP_POST_COST_FRACTION = 12;

    double VOTE_YES_MIN_EQ = 0.48;
    double VOTE_YES_NOISE_MIN = 0.2;
    double VOTE_YES_NOISE_MAX = 0.30;
    double VOTE_NO_MAX_EQ = 0.28;
    double VOTE_NO_NOISE_MIN = 0.2;
    double VOTE_NO_NOISE_MAX = 0.25;
    int VOTE_MIN_POT_BB_MULT = 1;

    double ACT_VAL_MONSTER_BASE_FRAC = 0.8;
    double ACT_VAL_MONSTER_EQ_MULT = 1.0;
    double ACT_VAL_MONSTER_EQ = 0.70;
    double ACT_VAL_THIN_EQ = 0.40;
    double ACT_VAL_THIN_SIZE = 0.55;
    int ACT_VAL_THIN_FREQ_MAX = 1;
    double ACT_BLUFF_MAX_EQ = 0.30;
    int ACT_BLUFF_MIN_STREET = 2;
    int ACT_BLUFF_SAFE_STACK_MULT = 3;
    double ACT_BLUFF_NIT_MASSIVE = 0.70;
    int ACT_BLUFF_FREQ_MASSIVE = 3;
    int ACT_BLUFF_FREQ_STD = 6;
    double ACT_BLUFF_NIT_3WAY = 0.55;
    int ACT_BLUFF_FREQ_3WAY = 10;
    double ACT_BLUFF_SIZE = 0.45;

    double DEF_RERAISE_MONSTER_EQ = 0.93;
    double DEF_ALLIN_MONSTER_EQ = 0.90;
    double DEF_RERAISE_SIZE = 0.80;
    double DEF_EV_MARGIN = 0.12;
    double DEF_VAL_RAISE_EQ = 0.75;
    double DEF_VAL_RAISE_SIZE = 0.75;
    double DEF_SURVIVAL_EQ = 0.40;
    double DEF_IMPLIED_MARGIN = 0.02;
    int DEF_IMPLIED_STACK_FRACTION = 6;
    int DEF_BLUFF_RAISE_MULT = 3;

    int PROF_MIN_HANDS = 5;
    double PROF_DEFAULT_FOLD = 0.30;
    double PROF_RAISE_POT_CAP = 5.0;
    double PROF_ALLIN_POT_CAP = 10.0;
    int PROF_MIN_ACTIONS = 5;
    double PROF_DEFAULT_AGG = 0.30;
} HP;

enum Suit { SPADES = 0, HEARTS, DIAMONDS, CLUBS };
enum Rank { TWO = 2, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, TEN, JACK, QUEEN, KING, ACE };

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
    return ((c >> 8) & 0xF) + 2;
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

#include "../tools/ck_tables.h"

static int eval_5cards(uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4, uint32_t c5) {
    uint32_t q = (c1 | c2 | c3 | c4 | c5) >> 16;

    if (c1 & c2 & c3 & c4 & c5 & 0xF000) {
        return flushes[q];
    }

    short unique_val = unique5[q];
    if (unique_val != 0) {
        return unique_val;
    }

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
    int absolute_value;
    bool operator>(const HandScore& o)  const { return absolute_value >  o.absolute_value; }
    bool operator==(const HandScore& o) const { return absolute_value == o.absolute_value; }
    bool operator>=(const HandScore& o) const { return absolute_value >= o.absolute_value; }
    bool operator<(const HandScore& o)  const { return absolute_value <  o.absolute_value; }
};

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

// Monte Carlo Equity Evaluator — time-bounded, runs until deadline_ms is consumed.
static double estimate_equity(
    const Card& h0, const Card& h1,
    const std::vector<Card>& board,
    int num_opponents,
    int num_simulations,
    std::mt19937& rng,
    const std::vector<Card>& extra_dead = {},
    double deadline_ms = 80000.0)
{
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

    if ((int)remaining.size() < cards_needed)
        return 1.0 / (num_opponents + 1.0);

    int wins = 0, ties = 0, total = 0;

    // Time-bounded loop: run as many sims as possible within deadline_ms.
    // Falls back to the fixed num_simulations cap when no deadline is supplied.
    // Clock is checked only every 64 iterations to reduce overhead.
    auto t_start = time_now();
    int sim = 0;
    while (sim < num_simulations) {
        if ((sim & 63) == 0 && elapsed_ms(t_start) >= deadline_ms) break;
        auto deck = remaining;
        int n = (int)deck.size();
        int draw_count = std::min(cards_needed, n);
        for (int i = 0; i < draw_count; i++) {
            std::uniform_int_distribution<int> dist(i, n - 1);
            int j = dist(rng);
            std::swap(deck[i], deck[j]);
        }

        uint32_t full_board[5];
        for (int i = 0; i < (int)board.size(); i++) full_board[i] = board[i];
        int idx = 0;
        for (int i = (int)board.size(); i < 5; i++)
            full_board[i] = deck[idx++];

        uint32_t our_hand[7] = {
            full_board[0], full_board[1], full_board[2],
            full_board[3], full_board[4], h0, h1
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
        sim++;
    }

    if (total == 0) return 1.0 / (num_opponents + 1.0);
    return (wins + ties * 0.5) / total;
}

static double estimate_swap_equity(
    const Card& kept_card,
    const std::vector<Card>& board,
    int num_opponents,
    int num_simulations,
    std::mt19937& rng,
    const std::vector<Card>& extra_dead = {},
    double deadline_ms = 80000.0)
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

    int cards_needed = board_needed + 1 + (2 * num_opponents);

    if ((int)remaining.size() < cards_needed) {
        return 1.0 / (num_opponents + 1.0);
    }

    int wins = 0, ties = 0, total = 0;

    auto t_start = time_now();
    int sim = 0;
    while (sim < num_simulations) {
        if ((sim & 63) == 0 && elapsed_ms(t_start) >= deadline_ms) break;
        auto deck = remaining;
        int n = (int)deck.size();
        int draw_count = std::min(cards_needed, n);
        for (int i = 0; i < draw_count; i++) {
            std::uniform_int_distribution<int> dist(i, n - 1);
            int j = dist(rng);
            std::swap(deck[i], deck[j]);
        }

        uint32_t full_board[5];
        for (int i = 0; i < (int)board.size(); i++) full_board[i] = board[i];
        int idx = 0;
        for (int i = (int)board.size(); i < 5; i++)
            full_board[i] = deck[idx++];

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
        sim++;
    }

    return (wins + ties * 0.5) / total;
}



namespace PreFlop {

struct Entry { float eq; int tier; };

// [high_rank][low_rank][0=offsuit / 1=suited]; pairs stored at [r][r][0]
static Entry tbl[13][13][2];

static void init() {
    // Pocket pair HU equities (rank 0=22 to 12=AA)
    static const float PE[13] = {
        0.506f,0.524f,0.543f,0.565f,0.585f,0.610f,0.638f,
        0.665f,0.693f,0.722f,0.766f,0.820f,0.853f
    };
    auto tier_of = [](float eq) -> int {
        if (eq<0.520f) return 1;
        if (eq<0.570f) return 2;
        if (eq<0.620f) return 3;
        if (eq<0.700f) return 4;
        return 5;
    };
    for (int r = 0; r < 13; ++r)
        tbl[r][r][0] = tbl[r][r][1] = {PE[r], tier_of(PE[r])};
    for (int h = 1; h < 13; ++h) {
        for (int l = 0; l < h; ++l) {
            float base = 0.310f + 0.0225f*h + 0.0095f*l;
            int   gap  = h - l;
            float conn = (gap==1)?0.025f:(gap==2)?0.012f:0.0f;
            float eq_o = std::min(0.72f, base + conn);
            float eq_s = std::min(0.76f, base + conn + 0.040f);
            tbl[h][l][0] = {eq_o, tier_of(eq_o)};
            tbl[h][l][1] = {eq_s, tier_of(eq_s)};
        }
    }
}

// Rank idx: 0=2 .. 12=A (matches get_rank()-2)
static Entry lookup(Card c1, Card c2) {
    int h = std::max(get_rank(c1), get_rank(c2)) - 2;
    int l = std::min(get_rank(c1), get_rank(c2)) - 2;
    bool s = (get_suit(c1) == get_suit(c2));
    if (h == l) return tbl[h][h][0];
    return tbl[h][l][s ? 1 : 0];
}

// Geometric blend discount for multi-way pots
static float multi_eq(float eq1, int n_opp) {
    if (n_opp <= 0) return 1.0f;
    if (n_opp == 1) return eq1;
    float geo = std::pow(eq1, (float)n_opp);
    float lin = eq1 - 0.05f * (n_opp - 1);
    return std::max(0.08f, (geo + lin) * 0.5f);
}

} // namespace PreFlop

struct GameState {
    int num_players = 2;
    int my_seat = 0;
    int starting_chips = 1000;
    std::array<int, 4> swap_mults = {5, 15, 25, 50};
    int small_blind = 1;
    int big_blind = 2;

    Card hole[2]{};
    std::vector<Card> board;
    std::vector<int> chips;
    int dealer_seat = 0;
    int sb_seat = 0;
    int bb_seat = 0;
    int hand_num = 0;

    std::vector<bool> folded;
    std::vector<bool> eliminated;
    int pot_estimate = 0;

    int current_street = 0;

    std::vector<Card> dead_cards;

    std::vector<int> total_hands_seen;
    std::vector<int> total_folds;
    std::vector<int> total_raises;
    std::vector<int> total_calls;

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

    // Setup known dead cards
    void mark_dead(const Card& c) {
        for (auto& d : dead_cards) if (d == c) return;
        dead_cards.push_back(c);
    }

    std::vector<Card> mc_dead() const {
        std::vector<Card> dead = dead_cards;

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

            if (action_count[i] < HP.PROF_MIN_ACTIONS) continue;

            total += agg_score[i] / action_count[i];
            cnt++;
        }
        return cnt > 0 ? total / cnt : HP.PROF_DEFAULT_AGG;
    }
};

int main() {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    // Initialise the O(1) pre-flop equity lookup table
    PreFlop::init();

    if (const char* env = std::getenv("OPT_RAISE_BASE_WEIGHT")) HP.RAISE_BASE_WEIGHT = std::stod(env);
    if (const char* env = std::getenv("OPT_BLUFF_THRESH"))      HP.BLUFF_AGGR_THRESH = std::stod(env);
    if (const char* env = std::getenv("OPT_MIN_FOLD_RATE"))     HP.MIN_FOLD_RATE_BLUFF = std::stod(env);
    if (const char* env = std::getenv("OPT_RAISE_POT_WEIGHT"))  HP.RAISE_POT_WEIGHT = std::stod(env);
    if (const char* env = std::getenv("OPT_RAISE_STACK_WEIGHT")) HP.RAISE_STACK_WEIGHT = std::stod(env);
    if (const char* env = std::getenv("OPT_ALLIN_BASE_WEIGHT")) HP.ALLIN_BASE_WEIGHT = std::stod(env);
    if (const char* env = std::getenv("OPT_ALLIN_POT_WEIGHT"))  HP.ALLIN_POT_WEIGHT = std::stod(env);

    if (const char* env = std::getenv("OPT_CHEN_DIVISOR"))      HP.CHEN_DIVISOR = std::stod(env);
    if (const char* env = std::getenv("OPT_PREFLOP_BASE_EQ"))   HP.PREFLOP_BASE_EQ = std::stod(env);
    if (const char* env = std::getenv("OPT_PREFLOP_MULT_EQ"))   HP.PREFLOP_MULT_EQ = std::stod(env);
    if (const char* env = std::getenv("OPT_MULTIWAY_BASE_EXP")) HP.MULTIWAY_BASE_EXP = std::stod(env);
    if (const char* env = std::getenv("OPT_MULTIWAY_STEP_EXP")) HP.MULTIWAY_STEP_EXP = std::stod(env);
    if (const char* env = std::getenv("OPT_MC_ROLLOUTS_SWAP_PRE"))       HP.MC_ROLLOUTS_SWAP_PRE = std::stoi(env);
    if (const char* env = std::getenv("OPT_MC_ROLLOUTS_SWAP_POST"))      HP.MC_ROLLOUTS_SWAP_POST = std::stoi(env);
    if (const char* env = std::getenv("OPT_MC_ROLLOUTS_VOTE"))           HP.MC_ROLLOUTS_VOTE = std::stoi(env);
    if (const char* env = std::getenv("OPT_MC_ROLLOUTS_ACTION_DEFAULT")) HP.MC_ROLLOUTS_ACTION_DEFAULT = std::stoi(env);
    if (const char* env = std::getenv("OPT_MC_ROLLOUTS_ACTION_RIVER"))   HP.MC_ROLLOUTS_ACTION_RIVER = std::stoi(env);
    if (const char* env = std::getenv("OPT_PREFLOP_MIN_EQ"))             HP.PREFLOP_MIN_EQ = std::stod(env);
    if (const char* env = std::getenv("OPT_PREFLOP_MAX_EQ"))             HP.PREFLOP_MAX_EQ = std::stod(env);

    if (const char* env = std::getenv("OPT_SWAP_PRE_STAY_EQ")) HP.SWAP_PRE_STAY_EQ = std::stod(env);
    if (const char* env = std::getenv("OPT_SWAP_PRE_WEAK_EQ")) HP.SWAP_PRE_WEAK_EQ = std::stod(env);
    if (const char* env = std::getenv("OPT_SWAP_PRE_COST_FRACTION")) HP.SWAP_PRE_COST_FRACTION = std::stoi(env);
    if (const char* env = std::getenv("OPT_SWAP_POST_MAX_EQ"))   HP.SWAP_POST_MAX_EQ = std::stod(env);
    if (const char* env = std::getenv("OPT_SWAP_POST_COST_FRACTION")) HP.SWAP_POST_COST_FRACTION = std::stoi(env);

    if (const char* env = std::getenv("OPT_VOTE_YES_MIN_EQ"))    HP.VOTE_YES_MIN_EQ = std::stod(env);
    if (const char* env = std::getenv("OPT_VOTE_YES_NOISE_MIN")) HP.VOTE_YES_NOISE_MIN = std::stod(env);
    if (const char* env = std::getenv("OPT_VOTE_YES_NOISE_MAX")) HP.VOTE_YES_NOISE_MAX = std::stod(env);
    if (const char* env = std::getenv("OPT_VOTE_NO_MAX_EQ"))     HP.VOTE_NO_MAX_EQ = std::stod(env);
    if (const char* env = std::getenv("OPT_VOTE_NO_NOISE_MIN"))  HP.VOTE_NO_NOISE_MIN = std::stod(env);
    if (const char* env = std::getenv("OPT_VOTE_NO_NOISE_MAX"))  HP.VOTE_NO_NOISE_MAX = std::stod(env);
    if (const char* env = std::getenv("OPT_VOTE_MIN_POT_BB_MULT")) HP.VOTE_MIN_POT_BB_MULT = std::stoi(env);

    if (const char* env = std::getenv("OPT_ACT_VAL_MONSTER_BASE_FRAC")) HP.ACT_VAL_MONSTER_BASE_FRAC = std::stod(env);
    if (const char* env = std::getenv("OPT_ACT_VAL_MONSTER_EQ_MULT")) HP.ACT_VAL_MONSTER_EQ_MULT = std::stod(env);
    if (const char* env = std::getenv("OPT_ACT_VAL_MONSTER_EQ")) HP.ACT_VAL_MONSTER_EQ = std::stod(env);
    if (const char* env = std::getenv("OPT_ACT_VAL_THIN_EQ"))    HP.ACT_VAL_THIN_EQ = std::stod(env);
    if (const char* env = std::getenv("OPT_ACT_VAL_THIN_SIZE"))  HP.ACT_VAL_THIN_SIZE = std::stod(env);
    if (const char* env = std::getenv("OPT_ACT_BLUFF_MAX_EQ"))   HP.ACT_BLUFF_MAX_EQ = std::stod(env);
    if (const char* env = std::getenv("OPT_ACT_BLUFF_NIT_MASSIVE")) HP.ACT_BLUFF_NIT_MASSIVE = std::stod(env);
    if (const char* env = std::getenv("OPT_ACT_BLUFF_NIT_3WAY")) HP.ACT_BLUFF_NIT_3WAY = std::stod(env);
    if (const char* env = std::getenv("OPT_ACT_BLUFF_SIZE"))     HP.ACT_BLUFF_SIZE = std::stod(env);
    if (const char* env = std::getenv("OPT_ACT_VAL_THIN_FREQ_MAX"))      HP.ACT_VAL_THIN_FREQ_MAX = std::stoi(env);
    if (const char* env = std::getenv("OPT_ACT_BLUFF_MIN_STREET"))       HP.ACT_BLUFF_MIN_STREET = std::stoi(env);
    if (const char* env = std::getenv("OPT_ACT_BLUFF_SAFE_STACK_MULT"))  HP.ACT_BLUFF_SAFE_STACK_MULT = std::stoi(env);
    if (const char* env = std::getenv("OPT_ACT_BLUFF_FREQ_MASSIVE"))     HP.ACT_BLUFF_FREQ_MASSIVE = std::stoi(env);
    if (const char* env = std::getenv("OPT_ACT_BLUFF_FREQ_STD"))         HP.ACT_BLUFF_FREQ_STD = std::stoi(env);
    if (const char* env = std::getenv("OPT_ACT_BLUFF_FREQ_3WAY"))        HP.ACT_BLUFF_FREQ_3WAY = std::stoi(env);

    if (const char* env = std::getenv("OPT_DEF_RERAISE_MONSTER_EQ")) HP.DEF_RERAISE_MONSTER_EQ = std::stod(env);
    if (const char* env = std::getenv("OPT_DEF_ALLIN_MONSTER_EQ"))   HP.DEF_ALLIN_MONSTER_EQ = std::stod(env);
    if (const char* env = std::getenv("OPT_DEF_RERAISE_SIZE"))       HP.DEF_RERAISE_SIZE = std::stod(env);
    if (const char* env = std::getenv("OPT_DEF_EV_MARGIN"))          HP.DEF_EV_MARGIN = std::stod(env);
    if (const char* env = std::getenv("OPT_DEF_VAL_RAISE_EQ"))       HP.DEF_VAL_RAISE_EQ = std::stod(env);
    if (const char* env = std::getenv("OPT_DEF_VAL_RAISE_SIZE"))     HP.DEF_VAL_RAISE_SIZE = std::stod(env);
    if (const char* env = std::getenv("OPT_DEF_SURVIVAL_EQ"))        HP.DEF_SURVIVAL_EQ = std::stod(env);
    if (const char* env = std::getenv("OPT_DEF_IMPLIED_MARGIN"))     HP.DEF_IMPLIED_MARGIN = std::stod(env);
    if (const char* env = std::getenv("OPT_DEF_BLUFF_RAISE_MULT"))   HP.DEF_BLUFF_RAISE_MULT = std::stoi(env);
    if (const char* env = std::getenv("OPT_DEF_IMPLIED_STACK_FRACTION")) HP.DEF_IMPLIED_STACK_FRACTION = std::stoi(env);

    if (const char* env = std::getenv("OPT_PROF_DEFAULT_FOLD"))      HP.PROF_DEFAULT_FOLD = std::stod(env);
    if (const char* env = std::getenv("OPT_PROF_DEFAULT_AGG"))       HP.PROF_DEFAULT_AGG = std::stod(env);
    if (const char* env = std::getenv("OPT_PROF_MIN_HANDS"))             HP.PROF_MIN_HANDS = std::stoi(env);
    if (const char* env = std::getenv("OPT_PROF_RAISE_POT_CAP"))         HP.PROF_RAISE_POT_CAP = std::stod(env);
    if (const char* env = std::getenv("OPT_PROF_ALLIN_POT_CAP"))         HP.PROF_ALLIN_POT_CAP = std::stod(env);
    if (const char* env = std::getenv("OPT_PROF_MIN_ACTIONS"))           HP.PROF_MIN_ACTIONS = std::stoi(env);

    std::mt19937 rng(std::chrono::high_resolution_clock::now().time_since_epoch().count());

    GameState gs;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        // Capture decision clock at the start of every message for deadline checks
        TimePoint t0 = time_now();

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
            gs.dead_cards.clear();
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
                gs.pot_estimate += amt;
            } else if (action == "ALLIN") {
                int amt; iss >> amt;
                if (seat != gs.my_seat) {
                    gs.total_raises[seat]++;

                    double cur_pot = std::max(1, gs.pot_estimate);
                    double risk_ratio = std::min(HP.PROF_ALLIN_POT_CAP, (double)amt / cur_pot);

                    gs.agg_score[seat] += HP.ALLIN_BASE_WEIGHT + (risk_ratio * HP.ALLIN_POT_WEIGHT);
                }
                gs.pot_estimate += amt;
            }

        }

        else if (cmd == "SWAP_DONE") {

        }

        else if (cmd == "VOTE_RESULT") {

        }

        else if (cmd == "SHOWDOWN") {

        }

        else if (cmd == "WINNER") {

        }

        else if (cmd == "ELIMINATE") {
            int seat; iss >> seat;
            gs.eliminated[seat] = true;
        }

        else if (cmd == "GAME_OVER") {
            break;
        }

        // Swap Card Logic (Pre-Flop Heuristic / Post-Flop Monte Carlo)
        else if (cmd == "SWAP_PROMPT") {
            int cost, my_chips;
            iss >> cost >> my_chips;

            int num_opp = gs.count_active_opponents();

            if (gs.current_street == 0) {
                // Subflow: Pre-flop swap based on PreFlop lookup table equity.
                PreFlop::Entry pfe = PreFlop::lookup(gs.hole[0], gs.hole[1]);
                double equity = PreFlop::multi_eq(pfe.eq, num_opp);

                if (equity >= HP.SWAP_PRE_STAY_EQ || cost > my_chips) {
                    std::cout << "STAY" << std::endl;
                } else if (equity < HP.SWAP_PRE_WEAK_EQ && cost <= my_chips / HP.SWAP_PRE_COST_FRACTION) {

                    int swap_idx = (get_rank(gs.hole[0]) <= get_rank(gs.hole[1])) ? 0 : 1;
                    g_last_swap_index = swap_idx;
                    std::cout << "SWAP " << swap_idx << std::endl;
                } else {
                    std::cout << "STAY" << std::endl;
                }
            } else {
                // Subflow: Post-flop swap evaluating Monte Carlo equity pre- and post-swap.

                double equity = estimate_equity(gs.hole[0], gs.hole[1], gs.board,
                                                std::max(1, num_opp), HP.MC_ROLLOUTS_SWAP_PRE, rng, gs.mc_dead());

                if (equity < HP.SWAP_POST_MAX_EQ && cost <= my_chips / HP.SWAP_POST_COST_FRACTION) {

                    double eq_keep_0 = estimate_swap_equity(
                        gs.hole[0], gs.board, std::max(1, num_opp), HP.MC_ROLLOUTS_SWAP_POST, rng, gs.mc_dead(),
                        4.0 - elapsed_ms(t0));
                    double eq_keep_1 = estimate_swap_equity(
                        gs.hole[1], gs.board, std::max(1, num_opp), HP.MC_ROLLOUTS_SWAP_POST, rng, gs.mc_dead(),
                        4.0 - elapsed_ms(t0));

                    int swap_idx;
                    if (eq_keep_0 > eq_keep_1)       swap_idx = 1;
                    else if (eq_keep_1 > eq_keep_0)  swap_idx = 0;
                    else swap_idx = (get_rank(gs.hole[0]) <= get_rank(gs.hole[1])) ? 0 : 1;

                    g_last_swap_index = swap_idx;
                    std::cout << "SWAP " << swap_idx << std::endl;
                } else {
                    std::cout << "STAY" << std::endl;
                }
            }
        }

        // Vote Logic (Expected Value Delta)
        else if (cmd == "VOTE_PROMPT") {
            int my_chips;
            iss >> my_chips;

            int num_opp = gs.count_active_opponents();

            if (gs.board.empty()) {
                std::cout << "VOTE YES 0" << std::endl;
            } else {
                // Time-bounded MC equity for current board state.
                double equity = estimate_equity(gs.hole[0], gs.hole[1], gs.board,
                                                std::max(1, num_opp), HP.MC_ROLLOUTS_VOTE, rng, gs.mc_dead(),
                                                4.5 - elapsed_ms(t0));

                // Calculate EV delta: Compare current MC equity vs fair-share redraw equity.
                // Redraw equity uses the O(1) pre-flop table instead of the Chen formula.
                double current_val = equity * gs.pot_estimate;

                PreFlop::Entry pfe = PreFlop::lookup(gs.hole[0], gs.hole[1]);
                double pf_eq = PreFlop::multi_eq(pfe.eq, num_opp);
                double redraw_val = pf_eq * gs.pot_estimate;

                double ev_gap = current_val - redraw_val;

                std::uniform_real_distribution<double> yes_noise(HP.VOTE_YES_NOISE_MIN, HP.VOTE_YES_NOISE_MAX);
                std::uniform_real_distribution<double> no_noise(HP.VOTE_NO_NOISE_MIN, HP.VOTE_NO_NOISE_MAX);

                // Subflow: We are ahead. Wager chips to protect our equity against redraws.
                if (equity >= HP.VOTE_YES_MIN_EQ && ev_gap > 0) {

                    int wager = (int)(ev_gap * yes_noise(rng));

                    if (gs.pot_estimate < gs.big_blind * HP.VOTE_MIN_POT_BB_MULT) wager = 0;

                    wager = std::max(0, std::min(wager, my_chips));
                    std::cout << "VOTE YES " << wager << std::endl;

                // Subflow: We are behind. Wager chips to destroy the board and force a redraw.
                } else if (equity < HP.VOTE_NO_MAX_EQ && ev_gap < 0) {

                    int wager = (int)(std::abs(ev_gap) * no_noise(rng));

                    if (gs.pot_estimate < gs.big_blind * HP.VOTE_MIN_POT_BB_MULT) wager = 0;

                    wager = std::max(0, std::min(wager, my_chips));
                    std::cout << "VOTE NO " << wager << std::endl;

                } else {
                    std::cout << "VOTE YES 0" << std::endl;
                }
            }
        }

        // Action Logic (Aggressor/Defender Trees)
        else if (cmd == "ACTION_PROMPT") {
            int my_chips, current_bet, my_bet, min_raise, pot;
            iss >> my_chips >> current_bet >> my_bet >> min_raise >> pot;

            int to_call = current_bet - my_bet;
            int num_opp = gs.count_active_opponents();

            double equity;

            if (gs.board.empty()) {
                // Pre-flop: O(1) table lookup, no MC needed.
                PreFlop::Entry pfe = PreFlop::lookup(gs.hole[0], gs.hole[1]);
                equity = PreFlop::multi_eq(pfe.eq, num_opp);
            } else {
                // Post-flop: time-bounded MC, leaving 2ms safety margin.
                double deadline = (gs.current_street == 3) ? 7.5 : 6.0;
                int sim_cap = (gs.current_street == 3) ? HP.MC_ROLLOUTS_ACTION_RIVER : HP.MC_ROLLOUTS_ACTION_DEFAULT;
                equity = estimate_equity(gs.hole[0], gs.hole[1], gs.board,
                                         std::max(1, num_opp), sim_cap, rng, gs.mc_dead(), deadline - elapsed_ms(t0));
            }

            double pot_odds = (to_call > 0) ? (double)to_call / (pot + to_call) : 0.0;

            if (to_call <= 0) {
                // Subflow: Aggressor Logic (We are first to act or checked to).

                // Monster hand: Value bet heavily based on equity advantage.
                if (equity >= HP.ACT_VAL_MONSTER_EQ) {

                    int raise_amt = min_raise;

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
                // Marginal hand: Mix thin value bets and checks randomly.
                } else if (equity >= HP.ACT_VAL_THIN_EQ) {

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
                // Mediocre hand (Showdown value): Check down.
                } else if (equity >= HP.ACT_BLUFF_MAX_EQ) {

                    std::cout << "CHECK" << std::endl;
                // Trash hand: Attempt mathematically EV+ bluffs against tight opponents.
                // Poor EV: Steal pot via semi-bluff reraise if conditions allow, else fold.
                } else {

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
                // Subflow: Defender Logic (We are facing a bet).

                // Monster hand: Reraise or go all-in.
                if (equity >= HP.DEF_RERAISE_MONSTER_EQ) {

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
                // Good EV: Call or thin-value reraise.
                } else if (equity > pot_odds + HP.DEF_EV_MARGIN) {

                    if (equity >= HP.DEF_VAL_RAISE_EQ && min_raise <= my_chips + my_bet) {

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

                        if (equity >= HP.DEF_SURVIVAL_EQ) {
                            std::cout << "ALLIN" << std::endl;
                        } else {
                            std::cout << "FOLD" << std::endl;
                        }
                    }
                // Implied odds: Call if bet is small relative to stack.
                } else if (equity > pot_odds - HP.DEF_IMPLIED_MARGIN) {

                    if (to_call <= my_chips / HP.DEF_IMPLIED_STACK_FRACTION && to_call <= my_chips) {
                        std::cout << "CALL" << std::endl;
                    } else {
                        std::cout << "FOLD" << std::endl;
                    }
                // Trash hand: Attempt mathematically EV+ bluffs against tight opponents.
                // Poor EV: Steal pot via semi-bluff reraise if conditions allow, else fold.
                } else {

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

