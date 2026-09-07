#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/DamageSystem.h"
#include "core/Deck.h"
#include "core/DistanceCalculator.h"
#include "core/CardSelectionRequest.h"
#include "core/EventDispatcher.h"
#include "core/GameAction.h"
#include "core/GameState.h"
#include "core/RandomGenerator.h"
#include "core/ResponseRequest.h"

namespace sanguosha {

struct CardUseContext {
    PlayerId source;
    CardId cardId;
    CardType cardType;
    std::vector<PlayerId> targets;
    int slashDamage {1};
    DamageNature damageNature {DamageNature::Normal};
    bool ignoreArmor {false};
    std::size_t currentTargetIndex {0};
    std::vector<PlayerId> doubleSwordResolvedTargets;
};

// A resolved Slash damage payload, retained only long enough for an explicitly
// authorized post-Dodge effect to apply the original hit without asking for
// Dodge or consuming another Slash.
struct SlashDamageSnapshot {
    PlayerId source;
    PlayerId target;
    CardType cardType {CardType::Slash};
    int damageAmount {1};
    DamageNature damageNature {DamageNature::Normal};
    bool ignoreArmor {false};
    bool blackNormalSlash {false};
    bool gudingBladeBonus {false};
};

struct Recover {
    PlayerId source;
    PlayerId target;
    int amount;
};
enum class EquipmentEffectType { IgnoreArmor, SlashConvertedToFire, BonusDamage, DamagePrevented, FireDamageIncreased, DamageCapped, HealOnLeave, MultiTargetEnabled };
struct EquipmentEffectEvent { std::uint64_t eventId {}; std::string equipmentName; EquipmentEffectType effect {}; PlayerId ownerId {}; PlayerId targetId {}; int value {}; CardType relatedCard {CardType::Slash}; };

struct DyingContext {
    PlayerId dyingPlayer;
    std::optional<PlayerId> damageSource;
    int requiredRecovery;
    std::vector<PlayerId> rescueOrder;
    std::size_t currentRescuerIndex {0};
};

struct DuelContext {
    PlayerId source;
    PlayerId target;
    PlayerId currentResponder;
    CardId duelCardId;
};
struct QinglongContext { PlayerId source; PlayerId target; };
struct AxeContext { PlayerId source; PlayerId target; std::uint64_t requestId {0}; };
struct KirinBowContext { PlayerId source; PlayerId target; std::uint64_t requestId {0}; };
struct DoubleSwordContext { PlayerId source; PlayerId target; std::uint64_t requestId {0}; };
struct IceSwordContext { PlayerId source; PlayerId target; std::uint64_t requestId {0}; SlashDamageSnapshot snapshot; bool activated {false}; int discardsRemaining {0}; };
enum class MultiTargetEffectType { BarbarianInvasion, ArrowBarrage };
struct MultiTargetEffectContext {
    MultiTargetEffectType type;
    PlayerId source;
    CardId cardId;
    std::vector<PlayerId> orderedTargets;
    std::size_t currentTargetIndex {0};
    ResponseType requiredResponse;
    bool awaitingNullification {false};
};
struct HarvestContext {
    PlayerId source;
    std::vector<PlayerId> targetOrder;
    std::size_t currentTargetIndex {0};
    std::vector<std::shared_ptr<Card>> pool;
    std::uint64_t currentRequestId {0};
    bool awaitingNullification {false};
};
struct BorrowedSwordContext {
    PlayerId source;
    PlayerId weaponHolder;
    PlayerId attackTarget;
    CardId weaponCardId;
    std::uint64_t requestId {0};
};
struct TrickResolutionContext {
    PlayerId source;
    CardId cardId;
    CardType cardType;
    std::optional<PlayerId> effectTarget;
};
struct NullificationChainContext {
    std::vector<PlayerId> responderOrder;
    std::size_t currentResponderIndex {0};
    int nullificationCount {0};
    PlayerId roundStartAfter;
    bool initialRound {true};
};
struct JudgmentPhaseContext {
    PlayerId player;
    std::vector<CardId> delayedTrickIds;
    std::size_t currentIndex {0};
    std::shared_ptr<Card> delayedTrick;
};
struct ChainDamageContext {
    PlayerId source;
    PlayerId originalTarget;
    int amount;
    DamageNature nature;
    std::vector<PlayerId> processedPlayers;
    std::vector<PlayerId> propagationOrder;
    std::size_t currentIndex {0};
};
struct FireAttackContext {
    PlayerId source;
    PlayerId target;
    std::optional<CardId> revealedCardId;
};
struct JudgmentContext {
    PlayerId player;
    CardId delayedTrickId;
    CardType delayedTrickType;
    std::shared_ptr<Card> judgmentCard;
    bool succeeded {false};
    std::size_t index {0};
};
struct IdentityDistribution { int lordCount; int loyalistCount; int rebelCount; int renegadeCount; };
[[nodiscard]] IdentityDistribution identityDistributionForPlayerCount(int playerCount);

class GameEngine {
public:
    void createGame(const std::vector<std::string> &playerNames,
                    const std::vector<PlayerIdentity> &identities = {},
                    const std::vector<PlayerControlType> &controlTypes = {},
                    GameMode mode = GameMode::FreeForAll);
    void startGame();
    void restart();
    ActionResult submitAction(const EndPlayPhaseAction &action);
    ActionResult submitAction(const DiscardAction &action);
    ActionResult submitAction(const PlayCardAction &action);
    ActionResult submitAction(const PlayVirtualSlashAction &action);
    ActionResult submitAction(const RespondVirtualSlashAction &action);
    ActionResult submitAction(const RespondAction &action);
    ActionResult submitAction(const SelectCardsAction &action);

    const std::vector<std::shared_ptr<Player>> &players() const noexcept;
    const Deck &deck() const noexcept;
    const Player *player(PlayerId playerId) const noexcept;
    const Player *currentPlayer() const noexcept;
    Phase currentPhase() const noexcept;
    int turnNumber() const noexcept;
    int requiredDiscardCount() const noexcept;
    int slashUsedThisPhase() const noexcept;
    [[nodiscard]] std::vector<PlayerId> virtualSlashTargets(PlayerId playerId) const;
    [[nodiscard]] std::vector<std::pair<PlayerId, PlayerId>> borrowedSwordTargetPairs(PlayerId playerId, const CardId& cardId) const;
    bool gameOver() const noexcept;
    std::optional<PlayerId> winner() const noexcept;
    WinningSide winningSide() const noexcept;
    GameMode gameMode() const noexcept;
    const std::vector<PlayerId>& winningPlayers() const noexcept;
    std::optional<PlayerId> nextAlivePlayer(PlayerId from) const noexcept;
    const std::optional<ResponseRequest> &pendingResponse() const noexcept;
    const std::optional<CardSelectionRequest> &pendingCardSelection() const noexcept;
    const std::optional<DyingContext> &dyingContext() const noexcept;
    const std::optional<DuelContext> &duelContext() const noexcept;
    const std::optional<QinglongContext> &qinglongContext() const noexcept;
    const std::optional<AxeContext> &axeContext() const noexcept;
    const std::optional<KirinBowContext> &kirinBowContext() const noexcept;
    const std::optional<DoubleSwordContext> &doubleSwordContext() const noexcept;
    const std::optional<IceSwordContext> &iceSwordContext() const noexcept;
    const std::optional<MultiTargetEffectContext> &multiTargetEffect() const noexcept;
    const std::optional<HarvestContext> &harvestContext() const noexcept;
    const std::optional<BorrowedSwordContext> &borrowedSwordContext() const noexcept;
    const std::optional<TrickResolutionContext> &trickResolution() const noexcept;
    const std::optional<NullificationChainContext> &nullificationChain() const noexcept;
    const std::optional<ChainDamageContext> &chainDamageContext() const noexcept;
    const std::optional<FireAttackContext> &fireAttackContext() const noexcept;
    const std::optional<JudgmentContext> &judgmentContext() const noexcept;
    const std::optional<JudgmentContext> &latestJudgment() const noexcept;
    const std::vector<std::string> &logEntries() const noexcept;
    void appendPublicLog(std::string entry);
    const std::vector<EquipmentEffectEvent>& equipmentEffects() const noexcept;
    const std::vector<GameEvent> &eventHistory() const noexcept;
    void subscribeEvents(EventDispatcher::Listener listener);
    bool canPlayCard(PlayerId playerId, const CardId &cardId) const;
    bool canPlayCardOnTarget(PlayerId playerId, const CardId &cardId, PlayerId targetId) const;
    [[nodiscard]] int minTargetsForCard(PlayerId playerId, const CardId &cardId) const;
    [[nodiscard]] int maxTargetsForCard(PlayerId playerId, const CardId &cardId) const;
    int distanceBetween(PlayerId source, PlayerId target) const;
    int attackRange(PlayerId playerId) const;
    ActionResult handleTimeout();
#ifdef SANGUOSHA_TESTING
    void testingSetHp(PlayerId playerId, int hp);
    void testingSetPlayerStatus(PlayerId playerId, PlayerStatus status);
    void testingSetChained(PlayerId playerId, bool chained);
    void testingSetIgnoreArmorForNextSlash(bool enabled);
    void testingCheckGameOver();
    void testingKillPlayer(PlayerId playerId);
    void testingEquip(PlayerId playerId, const std::shared_ptr<Card>& card);
    void testingRemoveEquipment(PlayerId playerId, EquipmentSlot slot);
    void testingSetIdentity(PlayerId playerId, PlayerIdentity identity);
    void testingSetGender(PlayerId playerId, Gender gender);
    void testingAddToDrawPile(const std::shared_ptr<Card>& card);
    void testingSetRandomSeed(std::uint32_t seed);
    std::shared_ptr<Card> testingDrawFromDeck();
    void testingStartSlash(PlayerId source, const std::vector<PlayerId>& targets, int baseDamage = 1,
                           DamageNature nature = DamageNature::Normal, bool ignoreArmor = false);
    [[nodiscard]] const std::optional<SlashDamageSnapshot>& testingDeferredSlashDamage() const noexcept;
    bool testingResumeDeferredSlashDamage();
    void testingApplyDamage(const Damage& damage);
#endif

private:
    void beginTurn();
    void enterPhase(Phase phase);
    void resolveJudgmentPhase();
    void continueJudgmentPhase();
    void resolveCurrentDelayedTrickJudgment();
    void drawCards(PlayerId playerId, int count);
    void completeTurn();
    void createDodgeRequest(const CardUseContext &context);
    bool createDoubleSwordRequest(const CardUseContext &context);
    void finishDoubleSword(bool targetDiscarded, const std::optional<CardId>& discardedCard = {});
    void cancelDoubleSwordContext(bool continueSlash);
    bool armorApplies(const Player &player, const CardUseContext &context, const char *armorName) const;
    bool vineArmorApplies(const Player &player, const CardUseContext &context) const;
    void createDuelRequest();
    void createMultiTargetResponse(); void resolveMultiTargetResponse(bool responded);
    void startMultiTargetEffect(MultiTargetEffectType type, PlayerId source, const CardId& cardId);
    void resolvePeachGarden(PlayerId source);
    void startHarvest(PlayerId source);
    void createHarvestSelection();
    void createHarvestCardSelection();
    void resolveHarvestSelection(const CardId& cardId);
    void finishHarvest();
    bool canUseKill(PlayerId source, PlayerId target, bool ignorePhaseLimit = false) const;
    void startBorrowedSword(PlayerId source, PlayerId weaponHolder, PlayerId attackTarget);
    void createBorrowedSwordResponse();
    void resolveBorrowedSwordDecline();
    void finishBorrowedSword();
    void beginTrickResolution(PlayerId source, const CardId& cardId, CardType type, std::optional<PlayerId> target = {});
    void createNullificationResponse();
    void resolveNullificationResponse(bool played);
    void finishNullificationChain();
    void createFireAttackReveal();
    void createFireAttackDiscard();
    void resolvePendingSlash(bool dodged);
    void createAxeDiscardRequest(const SlashDamageSnapshot& snapshot);
    void finishAxeDiscard(bool resumeDamage);
    void cancelAxeContext(bool continueSlash);
    void createKirinBowRequest(const Damage& damage);
    void finishKirinBow();
    void createIceSwordRequest(const SlashDamageSnapshot& snapshot);
    void finishIceSword(bool resumeDamage);
    void createIceSwordDiscardRequest();
    [[nodiscard]] SlashDamageSnapshot snapshotSlashDamage(const CardUseContext& context, PlayerId target) const;
    void resolveSlashDamage(const SlashDamageSnapshot& snapshot);
    bool resumeDeferredSlashDamage();
    void advanceSlashTarget();
    void resolvePendingDuel(bool respondedWithSlash);
    void createCardSelectionRequest(CardSelectionPurpose purpose, PlayerId requester, PlayerId target);
    void finishPendingCardUse();
    void applyDamage(const Damage &damage);
    void continueChainDamage();
    void applyRecover(const Recover &recover);
    std::shared_ptr<Card> removeEquipment(Player &player, EquipmentSlot slot, bool healSilverLion = true);
    void enterDying(PlayerId playerId, std::optional<PlayerId> damageSource);
    void requestNextRescue();
    void resolvePeachRescue(const ResponseRequest &request, const std::optional<CardId> &cardId);
    void killPlayer(PlayerId playerId, std::optional<PlayerId> killerOverride = {});
    void applyIdentityDeathConsequences(std::optional<PlayerId> killer, PlayerIdentity victimIdentity);
    void checkGameOver();
    std::shared_ptr<Player> findPlayer(PlayerId id) const;
    std::shared_ptr<Card> findHandCard(const Player &player, const CardId &id) const;
    bool hasSelectableCards(const Player &player) const;
    void log(std::string entry);
    void emitEvent(GameEventType type, std::optional<PlayerId> source = {}, std::optional<PlayerId> target = {}, std::string detail = {});
    void notifyInteractionCreated();
    void emitEquipmentEffect(std::string equipmentName, EquipmentEffectType effect, PlayerId ownerId, PlayerId targetId, int value = 0, CardType relatedCard = CardType::Slash);
    bool actionBlocked() const noexcept;

    std::vector<std::shared_ptr<Player>> players_;
    Deck deck_;
    RandomGenerator random_;
    DistanceCalculator distanceCalculator_;
    DamageSystem damageSystem_;
    EventDispatcher dispatcher_;
    std::size_t currentPlayerIndex_ {0};
    Phase currentPhase_ {Phase::Start};
    int turnNumber_ {0};
    int slashUsedThisPhase_ {0};
    bool started_ {false};
    GameMode gameMode_ {GameMode::FreeForAll};
    bool gameOver_ {false};
    std::optional<PlayerId> winner_;
    WinningSide winningSide_ {WinningSide::None};
    std::vector<PlayerId> winningPlayers_;
    std::uint64_t nextRequestId_ {1};
    std::optional<ResponseRequest> pendingResponse_;
    std::optional<CardSelectionRequest> pendingCardSelection_;
    std::optional<DyingContext> dyingContext_;
    std::optional<DuelContext> duelContext_;
    std::optional<QinglongContext> qinglongContext_;
    std::optional<AxeContext> axeContext_;
    std::optional<KirinBowContext> kirinBowContext_;
    std::optional<DoubleSwordContext> doubleSwordContext_;
    std::optional<IceSwordContext> iceSwordContext_;
    std::optional<MultiTargetEffectContext> multiTargetEffect_;
    std::optional<HarvestContext> harvestContext_;
    std::optional<BorrowedSwordContext> borrowedSwordContext_;
    std::optional<TrickResolutionContext> trickResolution_;
    std::optional<NullificationChainContext> nullificationChain_;
    std::optional<ChainDamageContext> chainDamageContext_;
    std::optional<FireAttackContext> fireAttackContext_;
    std::optional<JudgmentContext> judgmentContext_;
    std::optional<JudgmentPhaseContext> judgmentPhaseContext_;
    std::optional<JudgmentContext> latestJudgment_;
    std::optional<CardUseContext> pendingCardUse_;
    std::optional<SlashDamageSnapshot> deferredSlashDamage_;
    std::shared_ptr<Card> pendingCard_;
    std::unordered_map<CardId, PlayerId> delayedTrickSources_;
    bool skipDrawThisTurn_ {false};
    bool skipPlayThisTurn_ {false};
    bool testingIgnoreArmorForNextSlash_ {false};
    bool payingAxeCost_ {false};
    std::vector<std::string> logEntries_;
    std::vector<GameEvent> eventHistory_;
    std::uint64_t nextEquipmentEffectId_ {1};
    std::vector<EquipmentEffectEvent> equipmentEffects_;
};

} // namespace sanguosha
