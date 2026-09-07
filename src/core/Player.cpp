#include "core/Player.h"

#include <utility>
#include <algorithm>

namespace sanguosha {

Player::Player(PlayerId id, std::string name, Seat seat, PlayerIdentity identity, PlayerControlType controlType)
    : id_(id), name_(std::move(name)), seat_(seat), identity_(identity), controlType_(controlType)
{
}

PlayerId Player::id() const noexcept { return id_; }
const std::string &Player::name() const noexcept { return name_; }
int Player::seat() const noexcept { return seat_; }
PlayerIdentity Player::identity() const noexcept { return identity_; }
void Player::setIdentity(PlayerIdentity identity) { identity_ = identity; }
PlayerControlType Player::controlType() const noexcept { return controlType_; }
Gender Player::gender() const noexcept { return gender_; }
void Player::setGender(Gender gender) { gender_ = gender; }
int Player::hp() const noexcept { return hp_; }
int Player::maxHp() const noexcept { return maxHp_; }
bool Player::isAlive() const noexcept { return status_ == PlayerStatus::Alive; }
PlayerStatus Player::status() const noexcept { return status_; }
bool Player::hasWineBuff() const noexcept { return wineBuff_; }
bool Player::isChained() const noexcept { return chained_; }
const std::vector<std::shared_ptr<Card>> &Player::handCards() const noexcept { return handCards_; }
const std::vector<std::shared_ptr<Card>> &Player::judgmentCards() const noexcept { return judgmentCards_; }
bool Player::hasJudgmentCard(CardType type) const noexcept { return std::any_of(judgmentCards_.begin(), judgmentCards_.end(), [type](const auto& c) { return c && c->type() == type; }); }
namespace {
std::size_t slotIndex(EquipmentSlot slot) { return static_cast<std::size_t>(slot); }
}
std::shared_ptr<Card> Player::equipment(EquipmentSlot slot) const noexcept { return equipment_[slotIndex(slot)]; }
bool Player::hasEquipment(EquipmentSlot slot) const noexcept { return static_cast<bool>(equipment(slot)); }
std::vector<std::pair<EquipmentSlot, std::shared_ptr<Card>>> Player::equipmentCards() const
{
    std::vector<std::pair<EquipmentSlot, std::shared_ptr<Card>>> result;
    for (const auto slot : {EquipmentSlot::Weapon, EquipmentSlot::Armor, EquipmentSlot::OffensiveHorse, EquipmentSlot::DefensiveHorse}) {
        if (const auto card = equipment(slot)) result.emplace_back(slot, card);
    }
    return result;
}

void Player::addCard(const std::shared_ptr<Card> &card)
{
    if (card) handCards_.push_back(card);
}

void Player::addCards(const std::vector<std::shared_ptr<Card>> &cards)
{
    for (const auto &card : cards) addCard(card);
}

std::shared_ptr<Card> Player::removeCard(const CardId &id)
{
    const auto it = std::find_if(handCards_.begin(), handCards_.end(), [&id](const auto &card) {
        return card && card->id() == id;
    });
    if (it == handCards_.end()) return nullptr;
    auto card = *it;
    handCards_.erase(it);
    return card;
}

void Player::resetForNewGame()
{
    hp_ = maxHp_;
    status_ = PlayerStatus::Alive;
    wineBuff_ = false;
    chained_ = false;
    handCards_.clear();
    judgmentCards_.clear();
    equipment_.fill(nullptr);
}

void Player::receiveDamage(int amount) { hp_ -= amount; }
void Player::setStatus(PlayerStatus status) { status_ = status; }
void Player::recoverHp(int amount) { hp_ = std::min(maxHp_, hp_ + amount); }
void Player::setWineBuff(bool active) { wineBuff_ = active; }
void Player::setChained(bool chained) { chained_ = chained; }
void Player::toggleChained() { chained_ = !chained_; }
std::shared_ptr<Card> Player::equip(const std::shared_ptr<Card> &card)
{
    const auto slot = card ? equipmentSlotForCard(card->type()) : std::nullopt;
    if (!slot) return nullptr;
    auto previous = equipment_[slotIndex(*slot)];
    equipment_[slotIndex(*slot)] = card;
    return previous;
}
std::shared_ptr<Card> Player::removeEquipment(EquipmentSlot slot)
{
    auto card = equipment_[slotIndex(slot)];
    equipment_[slotIndex(slot)].reset();
    return card;
}
void Player::addJudgmentCard(const std::shared_ptr<Card>& card) { if (card) judgmentCards_.push_back(card); }
std::shared_ptr<Card> Player::removeJudgmentCard(const CardId& id) { const auto it = std::find_if(judgmentCards_.begin(), judgmentCards_.end(), [&id](const auto& c) { return c && c->id() == id; }); if (it == judgmentCards_.end()) return nullptr; auto card = *it; judgmentCards_.erase(it); return card; }

} // namespace sanguosha
