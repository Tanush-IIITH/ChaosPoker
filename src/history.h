#pragma once

#include "types.h"
#include <vector>
#include <string>
#include <sstream>

enum class EventType {
    HAND_START,
    DEAL_HOLE,
    DEAL_COMMUNITY,      // flop/turn/river dealt
    SWAP,                 // player swapped a card
    SWAP_STAY,            // player chose to stay
    VOTE,                 // player voted yes/no with amount
    VOTE_RESULT,          // vote outcome
    COMMUNITY_REDRAW,     // community cards redrawn after vote
    BET_ACTION,           // fold/check/call/raise/allin
    SHOWDOWN,             // player reveals cards
    WINNER,               // pot awarded
    ELIMINATE,            // player eliminated
};

struct Event {
    int sequence;         // ordering within the hand
    EventType type;
    int player;           // seat number, or -1 if not player-specific
    Street street;

    // flexible payload — fields used depend on EventType
    std::vector<Card> cards;       // cards involved
    int amount = 0;                // chips involved
    int amount2 = 0;               // second amount (e.g. vote: yes_total/no_total)
    std::string action;            // action string (FOLD, CALL, RAISE, YES, NO, etc.)
    HandRank hand_rank = HandRank::HIGH_CARD;
};

struct HandRecord {
    int hand_number = 0;
    int dealer_seat = 0;
    int small_blind_amount = 0;
    int big_blind_amount = 0;
    std::vector<Event> events;
    int next_seq = 0;

    void add_event(EventType type, int player, Street street) {
        events.push_back(Event{next_seq++, type, player, street, {}, 0, 0, {}, HandRank::HIGH_CARD});
    }

    Event& add_event_ref(EventType type, int player, Street street) {
        events.push_back(Event{next_seq++, type, player, street, {}, 0, 0, {}, HandRank::HIGH_CARD});
        return events.back();
    }

    std::string serialize() const;
};

class GameHistory {
public:
    void start_hand(int hand_number, int dealer, int sb_amount, int bb_amount);
    HandRecord& current_hand();
    const std::vector<HandRecord>& hands() const { return hands_; }
    std::string serialize_all() const;
    std::string pretty_print() const;

private:
    std::vector<HandRecord> hands_;
};
