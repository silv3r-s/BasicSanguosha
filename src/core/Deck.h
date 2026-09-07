#pragma once

#include <memory>
#include <vector>

#include "cards/Card.h"

namespace sanguosha { class RandomGenerator; }

namespace sanguosha {

class Deck {
public:
    void initialize(RandomGenerator &random);
    void shuffle(RandomGenerator &random);
    std::shared_ptr<Card> drawCard(RandomGenerator &random);
    std::vector<std::shared_ptr<Card>> drawCards(int count, RandomGenerator &random);
    void discard(const std::shared_ptr<Card> &card);
    void addToDrawPile(std::shared_ptr<Card> card);
    std::size_t drawPileSize() const noexcept;
    std::size_t discardPileSize() const noexcept;

private:
    std::vector<std::shared_ptr<Card>> drawPile_;
    std::vector<std::shared_ptr<Card>> discardPile_;
};

} // namespace sanguosha
