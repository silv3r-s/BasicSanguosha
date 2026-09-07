#include "ai/AIController.h"
#include "ai/AIActionPacing.h"
#include "ai/AIIdentityObjective.h"
#include "ai/AIIdentityStrategy.h"
#include "ai/AIResourcePlanner.h"
#include "ai/AITargetEvaluator.h"
#include "ai/AIThreatEvaluator.h"

#include <algorithm>
#ifdef SANGUOSHA_TESTING
#include <iostream>
#endif

#include <QTimer>

namespace sanguosha::ai {

std::optional<GameAction> BasicAIActionDecider::decideAction(const PlayerViewState& view, int requiredDiscardCount,
                                                             const TargetLegality& legalTarget,
                                                             const BorrowedSwordPairs& borrowedSwordPairs,
                                                             const PlayLegality& legalPlay) const
{
    if (view.gameOver) return std::nullopt;
    const AIIdentityObjective objective(view);
    const auto identityStrategy = AIIdentityStrategy::evaluate(view);
    const auto strategicallyAvoids = [&](PlayerId target, CardType cardType) {
        return objective.avoidsHostileTarget(target)
            && !(view.selfIdentity == PlayerIdentity::Renegade
                 && AIIdentityStrategy::targetModifier(identityStrategy, target, cardType) > 0);
    };
    if (view.response && view.response->isResponder) {
        const auto& response = *view.response;
        if (response.type == ResponseType::PeachRescue && response.targetId != view.selfPlayerId
            && (identityStrategy.enabled
                    ? !AIIdentityStrategy::shouldRescue(view, identityStrategy, response.targetId)
                    : objective.shouldDeclineRescue(response.targetId)))
            return RespondAction {view.selfPlayerId, response.requestId, std::nullopt};
        if ((response.type == ResponseType::BorrowedSwordSlash || response.type == ResponseType::QinglongSlash)
            && strategicallyAvoids(response.targetId, CardType::Slash))
            return RespondAction {view.selfPlayerId, response.requestId, std::nullopt};
        if (response.type == ResponseType::Nullification) {
            const bool protectsSelf = view.nullification && view.nullification->targetId && *view.nullification->targetId == view.selfPlayerId;
            if (identityStrategy.enabled
                    ? !AIIdentityStrategy::shouldNullify(view, identityStrategy)
                    : (objective.active() ? !objective.shouldNullify(view) : !protectsSelf))
                return RespondAction {view.selfPlayerId, response.requestId, std::nullopt};
            if (!AIResourcePlanner::shouldUseNullification(view))
                return RespondAction {view.selfPlayerId, response.requestId, std::nullopt};
        }
        const auto self = std::find_if(view.players.begin(), view.players.end(), [&] (const auto& player) { return player.id == view.selfPlayerId; });
        const bool hasSerpentSpear = self != view.players.end()
            && self->equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)]
            && self->equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)]->displayName == "丈八蛇矛";
        const bool slashResponse = response.type == ResponseType::Slash || response.type == ResponseType::BorrowedSwordSlash
            || response.type == ResponseType::QinglongSlash;
        if (slashResponse && response.selectableCards.empty() && hasSerpentSpear && view.ownHand.size() >= 2) {
            const bool emergency = self != view.players.end() && self->hp <= AIResourcePlanner::LowHpThreshold;
            const auto materials = AIResourcePlanner::serpentSpearMaterials(view, emergency);
            if (!materials.empty()) return RespondVirtualSlashAction {view.selfPlayerId, response.requestId, materials};
        }
        if (response.type == ResponseType::PeachRescue && response.targetId == view.selfPlayerId) {
            const auto wine = std::find_if(response.selectableCards.begin(), response.selectableCards.end(), [] (const auto& card) {
                return card.type == CardType::Wine;
            });
            if (wine != response.selectableCards.end()) return RespondAction {view.selfPlayerId, response.requestId, wine->id};
        }
        const auto chosen = std::min_element(response.selectableCards.begin(), response.selectableCards.end(), [&] (const auto& left, const auto& right) {
            const int leftValue = AIResourcePlanner::responseCost(view, left), rightValue = AIResourcePlanner::responseCost(view, right);
            return leftValue != rightValue ? leftValue < rightValue : left.id < right.id;
        });
        const std::optional<CardId> card = chosen == response.selectableCards.end() ? std::nullopt : std::optional<CardId>(chosen->id);
        return RespondAction {view.selfPlayerId, response.requestId, card};
    }
    if (view.response || view.currentTurnPlayer != view.selfPlayerId) return std::nullopt;
    if (view.currentPhase == Phase::Discard) {
        std::vector<CardId> cards;
        const int count = std::min(requiredDiscardCount, int(view.ownHand.size()));
        auto hand = view.ownHand;
        std::stable_sort(hand.begin(), hand.end(), [&] (const auto& left, const auto& right) {
            const int leftValue = AIResourcePlanner::keepValue(view, left)
                    + AIIdentityStrategy::cardKeepModifier(view, identityStrategy, left.type),
                rightValue = AIResourcePlanner::keepValue(view, right)
                    + AIIdentityStrategy::cardKeepModifier(view, identityStrategy, right.type);
            return leftValue != rightValue ? leftValue < rightValue : left.id < right.id;
        });
        for (int index = 0; index < count; ++index) cards.push_back(hand[static_cast<std::size_t>(index)].id);
        return DiscardAction {view.selfPlayerId, std::move(cards)};
    }
    if (view.currentPhase != Phase::Play) return std::nullopt;

    const auto self = std::find_if(view.players.begin(), view.players.end(), [&] (const auto& player) { return player.id == view.selfPlayerId; });
    if (self == view.players.end()) return EndPlayPhaseAction {view.selfPlayerId};
    const int slashFamilyCount = int(std::count_if(view.ownHand.begin(), view.ownHand.end(), [] (const auto& card) { return isSlashCard(card.type); }));
    struct Candidate { const CardView* card {}; std::vector<PlayerId> targets; int score {}; };
    std::vector<Candidate> candidates;
    int bestSlashKillOpportunity = -10000;
    const PublicPlayerView* bestSlashTarget = nullptr;
    for (const auto& slash : view.ownHand) if (isSlashCard(slash.type)) {
        for (const auto& target : view.players) {
            if (!strategicallyAvoids(target.id, slash.type) && legalTarget(slash.id, target.id)) {
                const int opportunity = AIThreatEvaluator::killOpportunityScore(view, target, true, slashFamilyCount);
                if (opportunity > bestSlashKillOpportunity) {
                    bestSlashKillOpportunity = opportunity;
                    bestSlashTarget = &target;
                }
            }
        }
    }
    for (const auto& card : view.ownHand) {
        if (legalPlay && !legalPlay(card.id)) continue;
        const auto lord = std::find_if(view.players.begin(), view.players.end(), [&] (const auto& p) {
            return p.alive && objective.avoidsHostileTarget(p.id);
        });
        const bool renegadeMayPressureLord = lord != view.players.end()
            && view.selfIdentity == PlayerIdentity::Renegade
            && AIIdentityStrategy::targetModifier(identityStrategy, lord->id, card.type) > 0;
        if (lord != view.players.end() && !renegadeMayPressureLord
            && (card.type == CardType::BarbarianInvasion || card.type == CardType::ArrowBarrage
                || (lord->chained && (card.type == CardType::FireSlash || card.type == CardType::ThunderSlash
                                     || card.type == CardType::FireAttack)))) continue;
        Candidate candidate {&card, {}, 0};
        if (card.type == CardType::Peach) {
            if (self->hp >= self->maxHp) continue;
            candidate.score = AIResourcePlanner::immediateUseValue(view, card);
            if (candidate.score == 0) continue;
        }
        else if (card.type == CardType::ExNihilo) candidate.score = AIResourcePlanner::immediateUseValue(view, card);
        else if (card.type == CardType::Wine) {
            if (slashFamilyCount == 0 || !AIResourcePlanner::shouldUseWine(view, bestSlashKillOpportunity)) continue;
            if (bestSlashTarget && bestSlashTarget->hp == 1 && bestSlashTarget->handCardCount == 0
                && !bestSlashTarget->equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Armor)]) continue;
            candidate.score = 135;
        }
        else if (card.type == CardType::PeachGarden || card.type == CardType::Harvest
                 || card.type == CardType::BarbarianInvasion || card.type == CardType::ArrowBarrage) {
            if (card.type == CardType::BarbarianInvasion || card.type == CardType::ArrowBarrage) {
                const int total = AITargetEvaluator::aoeValue(view, card.type, slashFamilyCount);
                if (total <= 0) continue;
                candidate.score = total / 10;
            } else {
                const int global = AITargetEvaluator::globalCardValue(view, card.type);
                if (global <= 0) continue;
                candidate.score = global;
            }
        }
        // Dodge and Nullification are response-only cards.  They deliberately
        // have no target metadata, but are not legal Play-phase actions.
        else if (card.type == CardType::Dodge || card.type == CardType::Nullification) continue;
        else if (card.type == CardType::Weapon || card.type == CardType::Armor || card.type == CardType::OffensiveHorse || card.type == CardType::DefensiveHorse) {
            const int replacementValue = AIResourcePlanner::equipmentReplacementValue(view, card);
            if (replacementValue == 0) continue;
            candidate.score = 18 + replacementValue / 10;
        }
        else if (card.minTargets == 0 && card.maxTargets == 0) candidate.score = card.type == CardType::Harvest ? (view.ownHand.size() <= 2 ? 65 : 30) : 20;
        else if (card.type == CardType::BorrowedSword) {
            auto pairs = borrowedSwordPairs ? borrowedSwordPairs(card.id) : std::vector<std::pair<PlayerId, PlayerId>> {};
            pairs.erase(std::remove_if(pairs.begin(), pairs.end(), [&] (const auto& pair) {
            return strategicallyAvoids(pair.first, CardType::BorrowedSword)
                    || strategicallyAvoids(pair.second, CardType::Slash);
            }), pairs.end());
            if (pairs.empty()) continue;
            const auto pairScore = [&] (const auto& pair) {
                const auto attacker = std::find_if(view.players.begin(), view.players.end(), [&] (const auto& player) { return player.id == pair.first; });
                const auto victim = std::find_if(view.players.begin(), view.players.end(), [&] (const auto& player) { return player.id == pair.second; });
                return attacker == view.players.end() || victim == view.players.end() ? -10000
                    : AITargetEvaluator::borrowedSwordPairScore(view, *attacker, *victim, slashFamilyCount);
            };
            const auto chosenPair = std::max_element(pairs.begin(), pairs.end(), [&] (const auto& left, const auto& right) {
                const int leftScore = pairScore(left), rightScore = pairScore(right);
                return leftScore != rightScore ? leftScore < rightScore : left > right;
            });
            candidate.targets = {chosenPair->first, chosenPair->second};
            candidate.score = pairScore(*chosenPair) / 10;
        } else {
            const auto targetSet = AITargetEvaluator::bestTargetSet(view, card, legalTarget);
            if (targetSet.targets.empty() && card.type != CardType::IronChain) continue;
            if (targetSet.targets.empty() && card.type == CardType::IronChain
                && std::none_of(view.players.begin(), view.players.end(), [&](const auto& player) {
                    return !strategicallyAvoids(player.id, card.type) && legalTarget(card.id, player.id);
                })) continue;
            candidate.targets = targetSet.targets;
            candidate.score = targetSet.finalScore() / 10;
            if (!candidate.targets.empty()) {
                const auto targetView = std::find_if(view.players.begin(), view.players.end(), [&] (const auto& p) { return p.id == candidate.targets.front(); });
                candidate.score += AIResourcePlanner::immediateUseValue(view, card, &*targetView) / 5;
                if ((card.type == CardType::Dismantlement || card.type == CardType::Snatch)
                    && slashFamilyCount > 0 && (targetView->equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Armor)]
                        || targetView->equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)])) candidate.score += 35;
            } else if (card.type == CardType::IronChain) {
                candidate.score = 42; // legal recast: draw, then re-evaluate from the new view
            }
        }
        candidate.score += AIIdentityStrategy::cardUseModifier(view, identityStrategy, card.type);
        candidates.push_back(std::move(candidate));
    }
    auto spearTargets = view.serpentSpearSlashTargets;
    spearTargets.erase(std::remove_if(spearTargets.begin(), spearTargets.end(), [&] (PlayerId id) {
        return strategicallyAvoids(id, CardType::Slash);
    }), spearTargets.end());
    if (candidates.empty() && !spearTargets.empty() && view.ownHand.size() >= 2) {
        const auto targetId = *std::max_element(spearTargets.begin(), spearTargets.end(), [&] (PlayerId left, PlayerId right) {
            const auto leftPlayer = std::find_if(view.players.begin(), view.players.end(), [&] (const auto& player) { return player.id == left; });
            const auto rightPlayer = std::find_if(view.players.begin(), view.players.end(), [&] (const auto& player) { return player.id == right; });
            const CardView virtualSlash {"virtual-slash", CardType::Slash, Suit::Spade, 1, "Slash", {}, 1, 1};
            const int leftScore = AITargetEvaluator::evaluate(view, virtualSlash, *leftPlayer, true, slashFamilyCount).finalScore();
            const int rightScore = AITargetEvaluator::evaluate(view, virtualSlash, *rightPlayer, true, slashFamilyCount).finalScore();
            return leftScore != rightScore ? leftScore < rightScore : left > right;
        });
        const auto materials = AIResourcePlanner::serpentSpearMaterials(view);
        if (!materials.empty()) return PlayVirtualSlashAction {view.selfPlayerId, materials, {targetId}};
    }
    if (candidates.empty()) return EndPlayPhaseAction {view.selfPlayerId};
    const auto chosen = std::max_element(candidates.begin(), candidates.end(), [] (const auto& left, const auto& right) {
        return left.score != right.score ? left.score < right.score : left.card->id > right.card->id;
    });
    return PlayCardAction {view.selfPlayerId, chosen->card->id, chosen->targets};
}

std::vector<SelectionOptionId> BasicAIActionDecider::decideSelection(const PlayerViewState& view, const CardSelectionView& selection) const
{
    if (selection.purpose == CardSelectionPurpose::Dismantlement || selection.purpose == CardSelectionPurpose::Snatch)
        return AITargetEvaluator::chooseTargetResourceSelection(view, selection);
    return AIResourcePlanner::chooseSelection(view, selection);
}

AIController::AIController(GameSession& session, PlayerId playerId, std::function<void()> actionApplied,
                           std::unique_ptr<IAIActionDecider> decider, QObject* parent)
    : QObject(parent), session_(session), playerId_(playerId), clientId_(session_.addClient(playerId)),
      actionApplied_(std::move(actionApplied)), decider_(std::move(decider))
{
    pacingTimer_.setSingleShot(true);
    connect(&pacingTimer_, &QTimer::timeout, this, [this] {
        scheduled_ = false;
        if (stopped_) return;
        const auto current = session_.viewFor(clientId_);
        const auto currentKey = interactionKey(current);
        if (current.gameOver) { stop(); return; }
        if (currentKey.empty() || currentKey != scheduledInteractionKey_) {
            scheduledInteractionKey_.clear();
            schedule();
            return;
        }
        scheduledInteractionKey_.clear();
        act();
    });
}

void AIController::schedule()
{
    if (stopped_ || scheduled_) return;
    const auto view = session_.viewFor(clientId_);
    if (view.gameOver) { stop(); return; }
    const auto key = interactionKey(view);
    if (key.empty() || key == lastInteractionKey_) return;
    scheduled_ = true;
    scheduledInteractionKey_ = key;
    const bool turnTransition = view.currentTurnPlayer == playerId_ && view.turnNumber != lastScheduledTurnNumber_;
    if (view.currentTurnPlayer == playerId_) lastScheduledTurnNumber_ = view.turnNumber;
    int delay = pacingEnabled_ ? AIActionPacing::delayFor(view, turnTransition) : 0;
#ifdef SANGUOSHA_TESTING
    if (testingDelayOverrideMs_ >= 0) delay = testingDelayOverrideMs_;
#endif
    pacingTimer_.start(delay);
}

void AIController::stop() { pacingTimer_.stop(); scheduledInteractionKey_.clear(); stopped_ = true; scheduled_ = false; }
void AIController::reset() { pacingTimer_.stop(); scheduledInteractionKey_.clear(); lastInteractionKey_.clear(); identityInference_.reset(); lastScheduledTurnNumber_ = 0; scheduled_ = false; stopped_ = false; }

#ifdef SANGUOSHA_TESTING
void AIController::setPacingEnabledForTesting(bool enabled, int delayOverrideMs) noexcept
{
    pacingTimer_.stop(); scheduled_ = false; scheduledInteractionKey_.clear();
    pacingEnabled_ = enabled; testingDelayOverrideMs_ = delayOverrideMs;
}
#endif

std::string AIController::interactionKey(const PlayerViewState& view) const
{
    if (view.response && view.response->isResponder) return "response:" + std::to_string(view.response->requestId);
    if (view.cardSelection) return "selection:" + std::to_string(view.cardSelection->requestId);
    if (view.currentTurnPlayer == playerId_ && (view.currentPhase == Phase::Play || view.currentPhase == Phase::Discard)) {
        return "turn:" + std::to_string(view.turnNumber) + ":" + std::to_string(static_cast<int>(view.currentPhase));
    }
    return {};
}

void AIController::act()
{
    scheduled_ = false;
    if (stopped_) return;
    const auto view = session_.viewFor(clientId_);
    if (view.gameOver) { stop(); return; }
    if (!view.cardSelection && !(view.response && view.response->isResponder)
        && !session_.canTakeTurnAction(clientId_)) return;
    const auto key = interactionKey(view);
    if (key.empty() || key == lastInteractionKey_) return;

    auto decisionView = view;
    identityInference_.rebuild(view);
    if (identityInference_.enabled()) {
        decisionView.inferenceModifiers.reserve(identityInference_.beliefs().size());
        for (const auto& belief : identityInference_.beliefs()) {
            auto modifier = identityInference_.targetModifier(view.selfIdentity, belief.playerId);
            modifier.loyalistScore = belief.loyalistScore;
            modifier.rebelScore = belief.rebelScore;
            modifier.renegadeScore = belief.renegadeScore;
            modifier.inferred = belief.inferred;
            decisionView.inferenceModifiers.push_back(modifier);
        }
    }

    ActionResult result;
    if (view.cardSelection) {
        const auto options = decider_->decideSelection(decisionView, *view.cardSelection);
        result = session_.submitCardSelection(clientId_, view.cardSelection->requestId, options);
    } else if (const auto action = decider_->decideAction(decisionView, session_.requiredDiscardCount(clientId_),
                                                           [this] (const CardId& cardId, PlayerId target) { return session_.canPlayCardOnTarget(clientId_, cardId, target); },
                                                           [this] (const CardId& cardId) { return session_.borrowedSwordTargetPairs(clientId_, cardId); },
                                                           [this] (const CardId& cardId) { return session_.canPlayCard(clientId_, cardId); })) {
        result = session_.submitAction(clientId_, *action);
#ifdef SANGUOSHA_TESTING
        if (!result.accepted && std::holds_alternative<PlayCardAction>(*action)) {
            const auto& play = std::get<PlayCardAction>(*action);
            std::cerr << "AI attempted card=" << play.cardId << " targets=" << play.targetIds.size() << '\n';
        }
#endif
    } else {
        return;
    }
    // A successful Play action changes the hand/state but can leave the phase
    // unchanged. Allow its next legal action; retain rejected keys to stop spam.
#ifdef SANGUOSHA_TESTING
    if (!result.accepted) std::cerr << "AI rejected action: player=" << playerId_ << " reason=" << result.message << '\n';
#endif
    lastInteractionKey_ = result.accepted ? std::string {} : key;
    if (result.accepted && actionApplied_) actionApplied_();
}

} // namespace sanguosha::ai
