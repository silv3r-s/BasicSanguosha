#include "core/Deck.h"

#include "core/RandomGenerator.h"
#include "cards/StandardDeckDefinition.h"

namespace sanguosha {

void Deck::initialize(RandomGenerator &random)
{
    drawPile_.clear();
    discardPile_.clear();

    drawPile_ = createStandardDeckCards();
    shuffle(random);
}

void Deck::shuffle(RandomGenerator &random)
{
    random.shuffle(drawPile_);
}

std::shared_ptr<Card> Deck::drawCard(RandomGenerator &random)
{
    if (drawPile_.empty() && !discardPile_.empty()) {
        drawPile_ = std::move(discardPile_);
        discardPile_.clear();
        shuffle(random);
    }
    if (drawPile_.empty()) return nullptr;
    auto card = drawPile_.back();
    drawPile_.pop_back();
    return card;
}

std::vector<std::shared_ptr<Card>> Deck::drawCards(int count, RandomGenerator &random)
{
    std::vector<std::shared_ptr<Card>> cards;
    for (int index = 0; index < count; ++index) {
        auto card = drawCard(random);
        if (!card) break;
        cards.push_back(std::move(card));
    }
    return cards;
}

void Deck::discard(const std::shared_ptr<Card> &card)
{
    if (card) discardPile_.push_back(card);
}

void Deck::addToDrawPile(std::shared_ptr<Card> card)
{
    drawPile_.push_back(std::move(card));
}

std::size_t Deck::drawPileSize() const noexcept { return drawPile_.size(); }
std::size_t Deck::discardPileSize() const noexcept { return discardPile_.size(); }

} // namespace sanguosha
