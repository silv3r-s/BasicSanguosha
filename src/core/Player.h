#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "cards/Card.h"

namespace sanguosha {

using PlayerId = int;
using Seat = int;

enum class PlayerIdentity { None, Lord, Loyalist, Rebel, Renegade };
enum class WinningSide { None, FreeForAll, LordSide, RebelSide, Renegade };
enum class GameMode { FreeForAll, Identity };
inline constexpr GameMode gameModeForPlayerCount(int playerCount) noexcept
{
    return playerCount >= 5 && playerCount <= 8 ? GameMode::Identity : GameMode::FreeForAll;
}
enum class PlayerControlType { Human, AI };
enum class Gender { Male, Female, Unknown };

enum class PlayerStatus {
    Alive,
    Dying,
    Dead
};

class Player {
public:
    Player(PlayerId id, std::string name, Seat seat, PlayerIdentity identity = PlayerIdentity::None,
           PlayerControlType controlType = PlayerControlType::Human);

    PlayerId id() const noexcept;
    const std::string &name() const noexcept;
    int seat() const noexcept;
    PlayerIdentity identity() const noexcept;
    PlayerControlType controlType() const noexcept;
    Gender gender() const noexcept;
    int hp() const noexcept;
    int maxHp() const noexcept;
    bool isAlive() const noexcept;
    PlayerStatus status() const noexcept;
    bool hasWineBuff() const noexcept;
    bool isChained() const noexcept;
    const std::vector<std::shared_ptr<Card>> &handCards() const noexcept;
    const std::vector<std::shared_ptr<Card>> &judgmentCards() const noexcept;
    bool hasJudgmentCard(CardType type) const noexcept;
    std::shared_ptr<Card> equipment(EquipmentSlot slot) const noexcept;
    bool hasEquipment(EquipmentSlot slot) const noexcept;
    std::vector<std::pair<EquipmentSlot, std::shared_ptr<Card>>> equipmentCards() const;
    void addCard(const std::shared_ptr<Card> &card);
    void addCards(const std::vector<std::shared_ptr<Card>> &cards);
    std::shared_ptr<Card> removeCard(const CardId &id);
    void resetForNewGame();

private:
    PlayerId id_;
    std::string name_;
    Seat seat_;
    PlayerIdentity identity_;
    PlayerControlType controlType_;
    Gender gender_ {Gender::Unknown};
    int hp_ {4};
    int maxHp_ {4};
    PlayerStatus status_ {PlayerStatus::Alive};
    bool wineBuff_ {false};
    bool chained_ {false};
    std::vector<std::shared_ptr<Card>> handCards_;
    std::vector<std::shared_ptr<Card>> judgmentCards_;
    std::array<std::shared_ptr<Card>, 4> equipment_;

    void receiveDamage(int amount);
    void setStatus(PlayerStatus status);
    void recoverHp(int amount);
    void setWineBuff(bool active);
    void setChained(bool chained);
    void toggleChained();
    std::shared_ptr<Card> equip(const std::shared_ptr<Card> &card);
    std::shared_ptr<Card> removeEquipment(EquipmentSlot slot);
    void addJudgmentCard(const std::shared_ptr<Card>& card);
    std::shared_ptr<Card> removeJudgmentCard(const CardId& id);
    void setIdentity(PlayerIdentity identity);
    void setGender(Gender gender);

    friend class DamageSystem;
    friend class GameEngine;
};

} // namespace sanguosha
