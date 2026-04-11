#pragma once

#include <string>
#include <array>
#include <cstdint>

enum class Suit : uint8_t { SPADES, HEARTS, DIAMONDS, CLUBS };
enum class Rank : uint8_t { TWO=2, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, TEN, JACK, QUEEN, KING, ACE };

enum class HandRank : uint8_t {
    HIGH_CARD,
    ONE_PAIR,
    TWO_PAIR,
    THREE_OF_A_KIND,
    STRAIGHT,
    FLUSH,
    FULL_HOUSE,
    FOUR_OF_A_KIND,
    STRAIGHT_FLUSH,
    ROYAL_FLUSH
};

enum class Street : uint8_t { PREFLOP, FLOP, TURN, RIVER };

struct Card {
    Rank rank;
    Suit suit;

    bool operator==(const Card& o) const { return rank == o.rank && suit == o.suit; }
    bool operator!=(const Card& o) const { return !(*this == o); }
};

inline char rank_to_char(Rank r) {
    constexpr const char* chars = "??23456789TJQKA";
    return chars[static_cast<int>(r)];
}

inline char suit_to_char(Suit s) {
    constexpr const char* chars = "shdc";
    return chars[static_cast<int>(s)];
}

inline std::string card_to_string(const Card& c) {
    return std::string(1, rank_to_char(c.rank)) + suit_to_char(c.suit);
}

inline Rank char_to_rank(char c) {
    switch (c) {
        case '2': return Rank::TWO;   case '3': return Rank::THREE;
        case '4': return Rank::FOUR;  case '5': return Rank::FIVE;
        case '6': return Rank::SIX;   case '7': return Rank::SEVEN;
        case '8': return Rank::EIGHT; case '9': return Rank::NINE;
        case 'T': return Rank::TEN;   case 'J': return Rank::JACK;
        case 'Q': return Rank::QUEEN; case 'K': return Rank::KING;
        case 'A': return Rank::ACE;
        default:  return Rank::TWO;
    }
}

inline Suit char_to_suit(char c) {
    switch (c) {
        case 's': return Suit::SPADES;   case 'h': return Suit::HEARTS;
        case 'd': return Suit::DIAMONDS; case 'c': return Suit::CLUBS;
        default:  return Suit::SPADES;
    }
}

inline Card string_to_card(const std::string& s) {
    return Card{char_to_rank(s[0]), char_to_suit(s[1])};
}

inline const char* hand_rank_to_string(HandRank hr) {
    switch (hr) {
        case HandRank::HIGH_CARD:       return "HIGH_CARD";
        case HandRank::ONE_PAIR:        return "ONE_PAIR";
        case HandRank::TWO_PAIR:        return "TWO_PAIR";
        case HandRank::THREE_OF_A_KIND: return "THREE_OF_A_KIND";
        case HandRank::STRAIGHT:        return "STRAIGHT";
        case HandRank::FLUSH:           return "FLUSH";
        case HandRank::FULL_HOUSE:      return "FULL_HOUSE";
        case HandRank::FOUR_OF_A_KIND:  return "FOUR_OF_A_KIND";
        case HandRank::STRAIGHT_FLUSH:  return "STRAIGHT_FLUSH";
        case HandRank::ROYAL_FLUSH:     return "ROYAL_FLUSH";
    }
    return "UNKNOWN";
}

constexpr int NUM_SUITS = 4;
constexpr int NUM_RANKS = 13;
constexpr int DECK_SIZE = 52;

constexpr std::array<Rank, NUM_RANKS> ALL_RANKS = {
    Rank::TWO, Rank::THREE, Rank::FOUR, Rank::FIVE, Rank::SIX,
    Rank::SEVEN, Rank::EIGHT, Rank::NINE, Rank::TEN,
    Rank::JACK, Rank::QUEEN, Rank::KING, Rank::ACE
};

constexpr std::array<Suit, NUM_SUITS> ALL_SUITS = {
    Suit::SPADES, Suit::HEARTS, Suit::DIAMONDS, Suit::CLUBS
};
