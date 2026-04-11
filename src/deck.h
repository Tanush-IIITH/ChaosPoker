#pragma once

#include "types.h"
#include <vector>
#include <random>

class Deck {
public:
    Deck();

    void shuffle(std::mt19937& rng);
    Card draw_one();
    std::vector<Card> draw(int n);
    int cards_remaining() const;
    void remove(const Card& c);

private:
    std::vector<Card> cards_;
};
