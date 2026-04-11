#include "deck.h"
#include <algorithm>
#include <stdexcept>

Deck::Deck() {
    cards_.reserve(DECK_SIZE);
    for (Suit s : ALL_SUITS) {
        for (Rank r : ALL_RANKS) {
            cards_.push_back(Card{r, s});
        }
    }
}

void Deck::shuffle(std::mt19937& rng) {
    std::shuffle(cards_.begin(), cards_.end(), rng);
}

Card Deck::draw_one() {
    if (cards_.empty()) {
        throw std::runtime_error("Deck is empty");
    }
    Card c = cards_.back();
    cards_.pop_back();
    return c;
}

std::vector<Card> Deck::draw(int n) {
    std::vector<Card> drawn;
    drawn.reserve(n);
    for (int i = 0; i < n; i++) {
        drawn.push_back(draw_one());
    }
    return drawn;
}

int Deck::cards_remaining() const {
    return static_cast<int>(cards_.size());
}

void Deck::remove(const Card& c) {
    auto it = std::find(cards_.begin(), cards_.end(), c);
    if (it != cards_.end()) {
        cards_.erase(it);
    }
}
