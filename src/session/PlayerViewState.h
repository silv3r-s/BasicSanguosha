#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cards/Card.h"
#include "core/CardSelectionRequest.h"
#include "core/GameState.h"
#include "core/GameEvent.h"
#include "core/Player.h"
#include "core/ResponseRequest.h"

namespace sanguosha {

using SessionClientId = std::uint64_t;
using SelectionOptionId = std::uint64_t;
enum class TimeoutKind { None, Play, Discard, Response, Selection };

struct CardView {
    CardId id;
    CardType type;
    Suit suit;
    int rank;
    std::string displayName;
    std::optional<int> attackRange;
    // Authoritative target limit for this card in the viewer's current state.
    // It is meaningful for cards in ownHand; all public cards retain the safe
    // single-target default.
    int minTargets {0};
    int maxTargets {1};
};

struct EquipmentView {
    std::array<std::optional<CardView>, 4> cardsBySlot;
};

struct PublicPlayerView {
    PlayerId id;
    std::string displayName;
    int hp;
    int maxHp;
    bool alive;
    bool dying;
    bool connected {true};
    bool chained {false};
    std::size_t handCardCount;
    EquipmentView equipment;
    std::vector<CardView> judgmentCards;
    Seat seat {0};
    PlayerControlType controlType {PlayerControlType::Human};
    std::optional<PlayerIdentity> identity;
};

struct ResponseView {
    ResponseType type;
    std::uint64_t requestId;
    PlayerId requester;
    PlayerId responder;
    bool isResponder {false};
    bool allowDecline {true};
    std::vector<CardView> selectableCards;
    std::string prompt;
    PlayerId targetId {0};
};

struct CardSelectionOptionView {
    SelectionOptionId optionId;
    CardZone zone;
    std::optional<EquipmentSlot> equipmentSlot;
    bool hidden {false};
    std::string displayName;
    std::optional<CardType> cardType;
};

struct CardSelectionView {
    std::uint64_t requestId;
    CardSelectionPurpose purpose;
    PlayerId targetId {0};
    int minCount {1};
    int maxCount {1};
    std::vector<CardSelectionOptionView> options;
    std::string prompt;
};
struct HarvestChoiceView {
    PlayerId playerId {0};
    CardView card;
};
struct HarvestView {
    PlayerId currentPicker {0};
    std::vector<CardView> pool;
    std::vector<HarvestChoiceView> choices;
};
struct JudgmentView {
    PlayerId playerId {0};
    CardId delayedTrickId;
    CardType delayedTrickType {CardType::Indulgence};
    CardView card;
    bool succeeded {false};
};
struct FireAttackView {
    PlayerId targetId {0};
    CardView revealedCard;
};
struct NullificationContextView {
    PlayerId sourceId {0};
    CardType trickType {CardType::Nullification};
    std::optional<PlayerId> targetId;
    int chainRound {1};
};

// Public, immutable snapshot data for a recent equipment-rule resolution.
// This intentionally mirrors only the values that may be shown to every player;
// it does not expose a GameEngine event object or any private interaction state.
enum class EquipmentEffectTypeView {
    IgnoreArmor,
    SlashConvertedToFire,
    BonusDamage,
    DamagePrevented,
    FireDamageIncreased,
    DamageCapped,
    HealOnLeave,
    MultiTargetEnabled
};

struct EquipmentEffectEventView {
    std::uint64_t eventId {0};
    std::string equipment;
    EquipmentEffectTypeView effect {EquipmentEffectTypeView::IgnoreArmor};
    PlayerId ownerId {0};
    PlayerId targetId {0};
    int value {0};
    CardType relatedCard {CardType::Slash};
};

// AI-only output derived from public behavior history. It carries no real
// hidden identity and is intentionally low-weight at the target layer.
enum class AIInferredIdentityView { Unknown, Lord, Loyalist, Rebel, Renegade };

struct AIInferenceModifierView {
    PlayerId playerId {0};
    int hostile {0};
    int friendly {0};
    int confidence {0};
    int loyalistScore {0};
    int rebelScore {0};
    int renegadeScore {0};
    AIInferredIdentityView inferred {AIInferredIdentityView::Unknown};
};

struct PlayerViewState {
    PlayerId selfPlayerId;
    int turnNumber;
    PlayerId currentTurnPlayer;
    Phase currentPhase;
    GameMode gameMode {GameMode::FreeForAll};
    std::vector<PublicPlayerView> players;
    std::vector<CardView> ownHand;
    std::vector<PlayerId> serpentSpearSlashTargets;
    std::optional<ResponseView> response;
    std::optional<CardSelectionView> cardSelection;
    std::optional<HarvestView> harvest;
    std::optional<JudgmentView> judgment;
    std::optional<FireAttackView> fireAttack;
    std::optional<NullificationContextView> nullification;
    std::size_t drawPileCount {0};
    std::size_t discardPileCount {0};
    bool gameOver {false};
    std::optional<PlayerId> winner;
    std::vector<std::string> visibleLogs;
    TimeoutKind timeoutKind {TimeoutKind::None};
    PlayerId timeoutPlayerId {0};
    int timeoutRemainingMs {0};
    PlayerIdentity selfIdentity {PlayerIdentity::None};
    WinningSide winningSide {WinningSide::None};
    std::vector<PlayerId> winningPlayers;
    std::vector<EquipmentEffectEventView> equipmentEffects;
    std::vector<GameEvent> publicEvents;
    std::vector<AIInferenceModifierView> inferenceModifiers;
};

} // namespace sanguosha
