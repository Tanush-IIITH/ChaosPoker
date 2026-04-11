#include "history.h"
#include <stdexcept>

void GameHistory::start_hand(int hand_number, int dealer, int sb_amount, int bb_amount) {
    HandRecord hr;
    hr.hand_number = hand_number;
    hr.dealer_seat = dealer;
    hr.small_blind_amount = sb_amount;
    hr.big_blind_amount = bb_amount;
    hands_.push_back(std::move(hr));
}

HandRecord& GameHistory::current_hand() {
    if (hands_.empty()) {
        throw std::runtime_error("No hand in progress");
    }
    return hands_.back();
}

static const char* event_type_str(EventType t) {
    switch (t) {
        case EventType::HAND_START:      return "HAND_START";
        case EventType::DEAL_HOLE:       return "DEAL_HOLE";
        case EventType::DEAL_COMMUNITY:  return "DEAL_COMMUNITY";
        case EventType::SWAP:            return "SWAP";
        case EventType::SWAP_STAY:       return "SWAP_STAY";
        case EventType::VOTE:            return "VOTE";
        case EventType::VOTE_RESULT:     return "VOTE_RESULT";
        case EventType::COMMUNITY_REDRAW:return "COMMUNITY_REDRAW";
        case EventType::BET_ACTION:      return "BET_ACTION";
        case EventType::SHOWDOWN:        return "SHOWDOWN";
        case EventType::WINNER:          return "WINNER";
        case EventType::ELIMINATE:       return "ELIMINATE";
    }
    return "UNKNOWN";
}

static const char* street_str(Street s) {
    switch (s) {
        case Street::PREFLOP: return "PREFLOP";
        case Street::FLOP:    return "FLOP";
        case Street::TURN:    return "TURN";
        case Street::RIVER:   return "RIVER";
    }
    return "UNKNOWN";
}

std::string HandRecord::serialize() const {
    std::ostringstream oss;
    oss << "=== HAND " << hand_number
        << " | dealer=" << dealer_seat
        << " | blinds=" << small_blind_amount << "/" << big_blind_amount
        << " ===\n";

    for (const auto& e : events) {
        oss << "  [" << e.sequence << "] "
            << event_type_str(e.type) << " "
            << street_str(e.street);

        if (e.player >= 0) {
            oss << " P" << e.player;
        }

        if (!e.action.empty()) {
            oss << " " << e.action;
        }

        if (e.amount != 0) {
            oss << " amt=" << e.amount;
        }
        if (e.amount2 != 0) {
            oss << " amt2=" << e.amount2;
        }

        for (const auto& c : e.cards) {
            oss << " " << card_to_string(c);
        }

        if (e.type == EventType::WINNER) {
            oss << " " << hand_rank_to_string(e.hand_rank);
        }

        oss << "\n";
    }
    return oss.str();
}

std::string GameHistory::serialize_all() const {
    std::ostringstream oss;
    for (const auto& h : hands_) {
        oss << h.serialize() << "\n";
    }
    return oss.str();
}

static std::string cards_str(const std::vector<Card>& cards) {
    std::string s;
    for (size_t i = 0; i < cards.size(); i++) {
        if (i > 0) s += " ";
        s += card_to_string(cards[i]);
    }
    return s;
}

static std::string repeat_char(char c, int n) {
    return std::string(n, c);
}

std::string GameHistory::pretty_print() const {
    std::ostringstream out;

    for (const auto& hand : hands_) {
        // hand header
        out << "\n" << repeat_char('=', 60) << "\n";
        out << "  HAND #" << hand.hand_number
            << "  |  Dealer: P" << hand.dealer_seat
            << "  |  Blinds: " << hand.small_blind_amount
            << "/" << hand.big_blind_amount << "\n";
        out << repeat_char('=', 60) << "\n";

        Street current_street = Street::PREFLOP;
        bool street_header_printed = false;

        for (const auto& e : hand.events) {
            // print street header when street changes
            if (e.street != current_street || !street_header_printed) {
                if (e.type != EventType::HAND_START) {
                    current_street = e.street;
                    out << "\n  --- " << street_str(current_street) << " ---\n";
                    street_header_printed = true;
                }
            }

            switch (e.type) {
            case EventType::HAND_START:
                // already printed in header
                break;

            case EventType::DEAL_HOLE:
                out << "  P" << e.player << " dealt: "
                    << cards_str(e.cards) << "\n";
                break;

            case EventType::DEAL_COMMUNITY:
                out << "  Board: " << cards_str(e.cards) << "\n";
                break;

            case EventType::SWAP:
                out << "  P" << e.player << " swapped "
                    << card_to_string(e.cards[0]) << " -> "
                    << card_to_string(e.cards[1])
                    << " (cost " << e.amount << ")\n";
                break;

            case EventType::SWAP_STAY:
                out << "  P" << e.player << " stayed\n";
                break;

            case EventType::VOTE:
                out << "  P" << e.player << " voted "
                    << e.action << " " << e.amount << "\n";
                break;

            case EventType::VOTE_RESULT:
                out << "  Vote: YES=" << e.amount << " NO=" << e.amount2
                    << " => " << e.action << "\n";
                break;

            case EventType::COMMUNITY_REDRAW:
                out << "  Redrawn: " << cards_str(e.cards) << "\n";
                break;

            case EventType::BET_ACTION:
                out << "  P" << e.player << " " << e.action;
                if (e.action == "RAISE" || e.action == "CALL" || e.action == "ALLIN") {
                    out << " " << e.amount;
                }
                out << "\n";
                break;

            case EventType::SHOWDOWN:
                out << "  P" << e.player << " shows "
                    << cards_str(e.cards) << "\n";
                break;

            case EventType::WINNER:
                out << "  >> P" << e.player << " wins "
                    << e.amount << " chips";
                if (e.hand_rank != HandRank::HIGH_CARD || e.amount > 0) {
                    out << " (" << hand_rank_to_string(e.hand_rank) << ")";
                }
                out << "\n";
                break;

            case EventType::ELIMINATE:
                out << "  ** P" << e.player << " eliminated **\n";
                break;
            }
        }
    }

    out << "\n" << repeat_char('=', 60) << "\n";
    return out.str();
}
