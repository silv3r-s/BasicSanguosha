#include "core/GameEngine.h"

#include <algorithm>
#include <set>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sanguosha {

namespace {
bool weaponNamed(const Player& player, const char* name)
{
    const auto weapon = player.equipment(EquipmentSlot::Weapon);
    return weapon && weapon->name() == name;
}
bool isNormalSlashForArmor(const CardUseContext& context)
{
    return context.cardType == CardType::Slash && context.damageNature == DamageNature::Normal;
}
const char* identityText(PlayerIdentity identity)
{
    switch (identity) {
    case PlayerIdentity::None: return "None";
    case PlayerIdentity::Lord: return "Lord";
    case PlayerIdentity::Loyalist: return "Loyalist";
    case PlayerIdentity::Rebel: return "Rebel";
    case PlayerIdentity::Renegade: return "Renegade";
    }
    return "Unknown";
}
}

IdentityDistribution identityDistributionForPlayerCount(int playerCount)
{
    switch (playerCount) {
    case 5: return {1, 1, 2, 1};
    case 6: return {1, 1, 3, 1};
    case 7: return {1, 2, 3, 1};
    case 8: return {1, 2, 4, 1};
    default: throw std::invalid_argument("Identity mode requires 5 to 8 players.");
    }
}

void GameEngine::createGame(const std::vector<std::string> &names, const std::vector<PlayerIdentity> &identities, const std::vector<PlayerControlType> &controlTypes, GameMode mode)
{
    if (names.size() < 2) throw std::invalid_argument("A game requires at least two players.");
    if (names.size() > 8) throw std::invalid_argument("A game supports at most eight players.");
    if (mode != gameModeForPlayerCount(static_cast<int>(names.size()))) throw std::invalid_argument("Game mode does not match the player count.");
    if (!identities.empty() && identities.size() != names.size()) throw std::invalid_argument("Each player needs an identity.");
    if (!controlTypes.empty() && controlTypes.size() != names.size()) throw std::invalid_argument("Each player needs a control type.");
    gameMode_ = mode;
    std::vector<PlayerIdentity> assigned = identities;
    if (assigned.empty()) {
        if (mode == GameMode::Identity) {
            const auto distribution = identityDistributionForPlayerCount(static_cast<int>(names.size()));
            assigned.insert(assigned.end(), distribution.lordCount, PlayerIdentity::Lord);
            assigned.insert(assigned.end(), distribution.loyalistCount, PlayerIdentity::Loyalist);
            assigned.insert(assigned.end(), distribution.rebelCount, PlayerIdentity::Rebel);
            assigned.insert(assigned.end(), distribution.renegadeCount, PlayerIdentity::Renegade);
            random_.shuffle(assigned);
        } else assigned.assign(names.size(), PlayerIdentity::None);
    }
    players_.clear();
    PlayerId id = 1; Seat seat = 0;
    for (std::size_t index = 0; index < names.size(); ++index) players_.push_back(std::make_shared<Player>(id++, names[index], seat++, assigned[index], controlTypes.empty() ? PlayerControlType::Human : controlTypes[index]));
    started_ = false; gameOver_ = false; winner_.reset(); winningSide_ = WinningSide::None; winningPlayers_.clear(); pendingResponse_.reset(); pendingCardSelection_.reset(); dyingContext_.reset(); duelContext_.reset(); qinglongContext_.reset(); axeContext_.reset(); kirinBowContext_.reset(); doubleSwordContext_.reset(); iceSwordContext_.reset(); multiTargetEffect_.reset(); harvestContext_.reset(); borrowedSwordContext_.reset(); trickResolution_.reset(); nullificationChain_.reset(); judgmentContext_.reset(); judgmentPhaseContext_.reset(); latestJudgment_.reset(); pendingCardUse_.reset(); deferredSlashDamage_.reset(); pendingCard_.reset(); delayedTrickSources_.clear(); skipDrawThisTurn_ = false; skipPlayThisTurn_ = false;
    logEntries_.clear(); eventHistory_.clear(); equipmentEffects_.clear(); nextGameEventId_ = 1; nextEquipmentEffectId_ = 1;
}

void GameEngine::startGame()
{
    if (players_.size() < 2) throw std::logic_error("Create players before starting the game.");
    for (const auto &player : players_) player->resetForNewGame();
    deck_.initialize(random_);
    logEntries_.clear(); eventHistory_.clear(); pendingResponse_.reset(); pendingCardSelection_.reset(); dyingContext_.reset(); duelContext_.reset(); qinglongContext_.reset(); axeContext_.reset(); kirinBowContext_.reset(); doubleSwordContext_.reset(); iceSwordContext_.reset(); multiTargetEffect_.reset(); harvestContext_.reset(); borrowedSwordContext_.reset(); trickResolution_.reset(); nullificationChain_.reset(); judgmentContext_.reset(); judgmentPhaseContext_.reset(); latestJudgment_.reset(); pendingCardUse_.reset(); deferredSlashDamage_.reset(); pendingCard_.reset(); delayedTrickSources_.clear(); skipDrawThisTurn_ = false; skipPlayThisTurn_ = false;
    nextGameEventId_ = 1;
    gameOver_ = false; winner_.reset(); winningSide_ = WinningSide::None; winningPlayers_.clear(); started_ = true; currentPlayerIndex_ = 0; turnNumber_ = 0;
    if (gameMode_ == GameMode::Identity) {
        const auto lord = std::find_if(players_.begin(), players_.end(), [](const auto& player) { return player->identity() == PlayerIdentity::Lord; });
        if (lord == players_.end()) throw std::logic_error("Identity game requires exactly one Lord.");
        currentPlayerIndex_ = static_cast<std::size_t>(std::distance(players_.begin(), lord));
    }
    log("Game started."); emitEvent(GameEventType::GameStarted);
    if (gameMode_ == GameMode::Identity) log(currentPlayer()->name() + " is Lord.");
    for (const auto &player : players_) drawCards(player->id(), 4);
    beginTurn();
}

void GameEngine::restart() { startGame(); }

ActionResult GameEngine::submitAction(const EndPlayPhaseAction &action)
{
    if (actionBlocked()) return {false, "The game cannot accept actions now."};
    if (!currentPlayer() || action.playerId != currentPlayer()->id()) return {false, "Only the current player can end Play."};
    if (currentPhase_ != Phase::Play) return {false, "The game is not in Play phase."};
    log(currentPlayer()->name() + " ended Play phase."); enterPhase(Phase::Discard); return {true, "Play phase ended."};
}

ActionResult GameEngine::submitAction(const DiscardAction &action)
{
    if (actionBlocked()) return {false, "The game cannot accept actions now."};
    if (!currentPlayer() || action.playerId != currentPlayer()->id()) return {false, "Only the current player can discard."};
    if (currentPhase_ != Phase::Discard) return {false, "The game is not in Discard phase."};
    if (static_cast<int>(action.cardIds.size()) != requiredDiscardCount()) return {false, "Incorrect discard count."};
    auto player = findPlayer(action.playerId); std::vector<std::shared_ptr<Card>> cards;
    for (const auto &id : action.cardIds) {
        auto card = player->removeCard(id);
        if (!card) { for (const auto &removed : cards) player->addCard(removed); return {false, "Selected card is not owned by this player."}; }
        cards.push_back(card);
    }
    for (const auto &card : cards) deck_.discard(card);
    if (!cards.empty()) log(player->name() + " discarded " + std::to_string(cards.size()) + " card(s).");
    enterPhase(Phase::Finish); return {true, "Cards discarded."};
}

ActionResult GameEngine::submitAction(const PlayCardAction &action)
{
    if (actionBlocked()) return {false, "The game cannot accept actions now."};
    if (!currentPlayer() || action.playerId != currentPlayer()->id()) return {false, "Only the current player can play cards."};
    if (currentPhase_ != Phase::Play) return {false, "Cards can only be played during Play phase."};
    auto source = findPlayer(action.playerId); auto card = source ? findHandCard(*source, action.cardId) : nullptr;
    if (!card) return {false, "Card is not in the player's hand."};
    if (const auto slot = equipmentSlotForCard(card->type())) {
        if (!action.targetIds.empty()) return {false, "Equipment does not require a target."};
        const int previousRange = attackRange(source->id());
        if (*slot == EquipmentSlot::Weapon && iceSwordContext_ && iceSwordContext_->source == source->id()
            && !iceSwordContext_->activated && weaponNamed(*source, "寒冰剑")) {
            finishIceSword(true);
        }
        const auto equipped = source->removeCard(card->id());
        const auto replaced = source->equip(equipped);
        if (replaced) {
            if (replaced->name() == "白银狮子" && source->hp() < source->maxHp()) {
                applyRecover(Recover {source->id(), source->id(), 1});
                emitEquipmentEffect("白银狮子", EquipmentEffectType::HealOnLeave, source->id(), source->id(), 1);
                log(source->name() + " recovered 1 HP as [Silver Lion] left the equipment area.");
            }
            deck_.discard(replaced);
            log(source->name() + " replaced equipped [" + replaced->name() + "].");
        }
        log(source->name() + " equipped [" + equipped->name() + "].");
        emitEvent(GameEventType::CardUsed, source->id(), source->id(), equipped->name(), equipped.get());
        if (*slot == EquipmentSlot::Weapon && previousRange != attackRange(source->id())) {
            log(source->name() + " attack range is now " + std::to_string(attackRange(source->id())) + ".");
        }
        return {true, "Equipment used."};
    }
    if (card->type() == CardType::Peach) {
        if (!action.targetIds.empty()) return {false, "Peach cannot target another player during Play."};
        if (source->hp() >= source->maxHp()) return {false, "Current HP is already full."};
        auto used = source->removeCard(card->id());
        log(source->name() + " used [Peach].");
        emitEvent(GameEventType::CardUsed, source->id(), source->id(), "Peach", card.get());
        deck_.discard(used);
        applyRecover(Recover {source->id(), source->id(), 1});
        return {true, "Peach used."};
    }
    if (card->type() == CardType::Wine) {
        if (!action.targetIds.empty()) return {false, "Wine cannot target another player."};
        if (source->hasWineBuff()) return {false, "Wine has already been used this turn."};
        deck_.discard(source->removeCard(card->id()));
        source->setWineBuff(true);
        log(source->name() + " used [Wine].");
        log(source->name() + "'s next [Slash] damage is +1.");
        emitEvent(GameEventType::CardUsed, source->id(), source->id(), "Wine", card.get());
        return {true, "Wine used."};
    }
    if (card->type() == CardType::ExNihilo) {
        if (!action.targetIds.empty()) return {false, "Ex Nihilo does not require a target."};
        pendingCard_ = source->removeCard(card->id()); pendingCardUse_ = CardUseContext {source->id(), card->id(), card->type(), {}, 1};
        log(source->name() + " used [Ex Nihilo].");
        emitEvent(GameEventType::CardUsed, source->id(), source->id(), "ExNihilo", card.get());
        beginTrickResolution(source->id(), card->id(), card->type());
        return {true, "Ex Nihilo is awaiting Nullification responses."};
    }
    if (card->type() == CardType::PeachGarden) {
        if (!action.targetIds.empty()) return {false, "Peach Garden does not require a target."};
        pendingCard_ = source->removeCard(card->id()); pendingCardUse_ = CardUseContext {source->id(), card->id(), card->type(), {}, 1};
        log(source->name() + " used [Peach Garden]."); emitEvent(GameEventType::CardUsed, source->id(), {}, "PeachGarden", card.get());
        beginTrickResolution(source->id(), card->id(), card->type()); return {true, "Peach Garden is awaiting Nullification responses."};
    }
    if (card->type() == CardType::Harvest) {
        if (!action.targetIds.empty()) return {false, "Harvest does not require a target."};
        pendingCard_ = source->removeCard(card->id()); pendingCardUse_ = CardUseContext {source->id(), card->id(), card->type(), {}, 1};
        log(source->name() + " used [Harvest]."); emitEvent(GameEventType::CardUsed, source->id(), {}, "Harvest", card.get()); startHarvest(source->id()); return {true, "Harvest is resolving each recipient."};
    }
    if (card->type() == CardType::IronChain) {
        if (action.targetIds.empty()) {
            deck_.discard(source->removeCard(card->id()));
            drawCards(source->id(), 1);
            log(source->name() + " recast [Iron Chain].");
            emitEvent(GameEventType::CardUsed, source->id(), {}, "IronChainRecast", card.get());
            return {true, "Iron Chain recast."};
        }
        if (action.targetIds.size() > 2) return {false, "Iron Chain allows one or two targets."};
        std::vector<std::shared_ptr<Player>> targets;
        for (const auto targetId : action.targetIds) {
            const auto target = findPlayer(targetId);
            if (!target || !target->isAlive()) return {false, "Iron Chain target is invalid."};
            if (std::any_of(targets.begin(), targets.end(), [targetId](const auto& candidate) { return candidate->id() == targetId; })) return {false, "Iron Chain targets must be distinct."};
            targets.push_back(target);
        }
        pendingCard_ = source->removeCard(card->id());
        pendingCardUse_ = CardUseContext {source->id(), card->id(), card->type(), action.targetIds, 1};
        log(source->name() + " used [Iron Chain].");
        emitEvent(GameEventType::CardUsed, source->id(), {}, "IronChain", card.get());
        beginTrickResolution(source->id(), card->id(), card->type());
        return {true, "Iron Chain is awaiting Nullification responses."};
    }
    if (card->type() == CardType::FireAttack) {
        if (action.targetIds.size() != 1) return {false, "Fire Attack requires exactly one target."};
        const auto target = findPlayer(action.targetIds.front());
        if (!target || !target->isAlive() || target->handCards().empty()) return {false, "Fire Attack target must have a hand card."};
        pendingCard_ = source->removeCard(card->id());
        pendingCardUse_ = CardUseContext {source->id(), card->id(), card->type(), action.targetIds, 1};
        log(source->name() + " used [Fire Attack].");
        emitEvent(GameEventType::CardUsed, source->id(), target->id(), "FireAttack", card.get());
        beginTrickResolution(source->id(), card->id(), card->type(), target->id());
        return {true, "Fire Attack is awaiting Nullification responses."};
    }
    if (card->type() == CardType::BorrowedSword) {
        if (action.targetIds.size() != 2) return {false, "Borrowed Sword requires a weapon holder and attack target."};
        const auto holder = findPlayer(action.targetIds[0]); const auto target = findPlayer(action.targetIds[1]);
        if (!holder || !target || holder->id() == source->id() || !holder->isAlive() || !target->isAlive() || !holder->equipment(EquipmentSlot::Weapon) || !canUseKill(holder->id(), target->id(), true)) return {false, "Borrowed Sword targets are invalid."};
        pendingCard_ = source->removeCard(card->id()); pendingCardUse_ = CardUseContext {source->id(), card->id(), card->type(), action.targetIds, 1};
        log(source->name() + " used [Borrowed Sword] on " + holder->name() + ", targeting " + target->name() + "."); emitEvent(GameEventType::CardUsed, source->id(), holder->id(), "BorrowedSword", card.get());
        beginTrickResolution(source->id(), card->id(), card->type(), holder->id()); return {true, "Borrowed Sword is awaiting Nullification responses."};
    }
    if (card->type() == CardType::Indulgence || card->type() == CardType::SupplyShortage || card->type() == CardType::Lightning) {
        if (action.targetIds.size() != 1) return {false, "Delayed trick requires exactly one target."};
        const auto target = findPlayer(action.targetIds.front());
        if (!target || !target->isAlive() || target->hasJudgmentCard(card->type())) return {false, "Delayed trick target is invalid or already has this trick."};
        pendingCard_ = source->removeCard(card->id()); pendingCardUse_ = CardUseContext {source->id(), card->id(), card->type(), action.targetIds, 1};
        log(source->name() + " used delayed trick [" + card->name() + "]."); emitEvent(GameEventType::CardUsed, source->id(), target->id(), card->name(), card.get());
        const auto trickName = pendingCard_->name(); delayedTrickSources_[pendingCard_->id()] = source->id(); target->addJudgmentCard(pendingCard_); pendingCard_.reset();
        log("[" + trickName + "] entered " + target->name() + "'s judgment zone."); finishPendingCardUse();
        return {true, "Delayed trick entered the judgment zone."};
    }
    if (card->type() == CardType::BarbarianInvasion || card->type() == CardType::ArrowBarrage) {
        if (!action.targetIds.empty()) return {false, "AOE cards do not require a target."};
        pendingCard_ = source->removeCard(card->id()); pendingCardUse_ = CardUseContext {source->id(), card->id(), card->type(), {}, 1};
        const auto type = card->type() == CardType::BarbarianInvasion ? MultiTargetEffectType::BarbarianInvasion : MultiTargetEffectType::ArrowBarrage;
        log(source->name() + (type == MultiTargetEffectType::BarbarianInvasion ? " used [Barbarian Invasion]." : " used [Arrow Barrage]."));
        emitEvent(GameEventType::CardUsed, source->id(), {}, card->name(), card.get()); startMultiTargetEffect(type, source->id(), card->id()); return {true, "AOE is resolving each target."};
    }
    const bool isSlash = isSlashCard(card->type());
    if (!isSlash && card->type() != CardType::Dismantlement
        && card->type() != CardType::Snatch && card->type() != CardType::Duel) {
        return {false, "This card cannot be actively used now."};
    }
    const bool halberd = isSlash && action.targetIds.size() > 1 && action.targetIds.size() <= 3 && source->handCards().size() == 1 && weaponNamed(*source, "方天画戟");
    if (action.targetIds.empty() || action.targetIds.size() > (halberd ? 3u : 1u)) return {false, "Invalid Slash target count."};
    std::set<PlayerId> uniqueTargets(action.targetIds.begin(), action.targetIds.end()); if (uniqueTargets.size() != action.targetIds.size()) return {false, "Duplicate Slash target."};
    for (const auto targetId : action.targetIds) if (!canPlayCardOnTarget(source->id(), card->id(), targetId)) return {false, "Target is invalid."};
    auto target = findPlayer(action.targetIds.front());
    if (isSlash) deferredSlashDamage_.reset();
    pendingCard_ = source->removeCard(card->id());
    const auto nature = card->type() == CardType::FireSlash ? DamageNature::Fire : card->type() == CardType::ThunderSlash ? DamageNature::Thunder : (isSlash && weaponNamed(*source, "朱雀羽扇") ? DamageNature::Fire : DamageNature::Normal);
    pendingCardUse_ = CardUseContext {source->id(), card->id(), card->type(), action.targetIds,
        isSlash && source->hasWineBuff() ? 2 : 1, nature,
        isSlash && (weaponNamed(*source, "青釭剑") || testingIgnoreArmorForNextSlash_)};
    testingIgnoreArmorForNextSlash_ = false;
    if (halberd) emitEquipmentEffect("方天画戟", EquipmentEffectType::MultiTargetEnabled, source->id(), 0, 3);
    if (isSlash && card->type() == CardType::Slash && nature == DamageNature::Fire) emitEquipmentEffect("朱雀羽扇", EquipmentEffectType::SlashConvertedToFire, source->id(), 0);
    emitEvent(GameEventType::CardUsed, source->id(), target->id(), card->name(), card.get());
    emitEvent(GameEventType::TargetSpecified, source->id(), target->id(), card->name());
    if (isSlash) {
        const int slashDamage = pendingCardUse_->slashDamage;
        source->setWineBuff(false);
        ++slashUsedThisPhase_;
        if (halberd) log(source->name() + " activated [Halberd] for multiple Slash targets.");
        if (card->type() == CardType::Slash && nature == DamageNature::Fire) {
            log(source->name() + " converted [Slash] to [Fire Slash] with [Vermilion Fan].");
        }
        if (isNormalSlashForArmor(*pendingCardUse_) && vineArmorApplies(*target, *pendingCardUse_)) {
            emitEquipmentEffect("藤甲", EquipmentEffectType::DamagePrevented, target->id(), target->id(), 0, CardType::Slash);
            log(target->name() + " was protected by [Vine Armor]."); advanceSlashTarget(); return {true, "Slash was nullified by Vine Armor."};
        }
        log(source->name() + (slashDamage == 2 ? " used [Wine] enhanced [Slash] targeting " : " used [Slash] targeting ") + target->name() + ".");
        createDodgeRequest(*pendingCardUse_);
        return {true, "Slash is awaiting a response."};
    }
    if (card->type() == CardType::Dismantlement || card->type() == CardType::Snatch || card->type() == CardType::Duel) {
        beginTrickResolution(source->id(), card->id(), card->type(), target->id());
        return {true, "Trick is awaiting Nullification responses."};
    }
    if (card->type() == CardType::Dismantlement) {
        log(source->name() + " used [Dismantlement] targeting " + target->name() + ".");
        createCardSelectionRequest(CardSelectionPurpose::Dismantlement, source->id(), target->id());
        return {true, "Dismantlement is awaiting a card selection."};
    }
    if (card->type() == CardType::Snatch) {
        log(source->name() + " used [Snatch] targeting " + target->name() + ".");
        createCardSelectionRequest(CardSelectionPurpose::Snatch, source->id(), target->id());
        return {true, "Snatch is awaiting a card selection."};
    }
    log(source->name() + " used [Duel] targeting " + target->name() + ".");
    duelContext_ = DuelContext {source->id(), target->id(), target->id(), card->id()};
    createDuelRequest();
    return {true, "Duel is awaiting a Slash response."};
}

ActionResult GameEngine::submitAction(const PlayVirtualSlashAction &action)
{
    if (actionBlocked() || !currentPlayer() || currentPlayer()->id() != action.playerId || currentPhase_ != Phase::Play) return {false, "Virtual Slash is not legal now."};
    auto source = findPlayer(action.playerId);
    if (!source || !weaponNamed(*source, "丈八蛇矛") || action.subcardIds.size() != 2 || action.targetIds.size() != 1) return {false, "Serpent Spear requires two cards and one target."};
    if (action.subcardIds[0] == action.subcardIds[1]) return {false, "Duplicate Serpent Spear subcard."};
    const auto target = findPlayer(action.targetIds.front());
    if (!target || !target->isAlive() || !canUseKill(source->id(), target->id())) return {false, "Virtual Slash target is invalid."};
    std::vector<std::shared_ptr<Card>> consumed;
    for (const auto &id : action.subcardIds) { auto card = source->removeCard(id); if (!card) { for (const auto &returned : consumed) source->addCard(returned); return {false, "Serpent Spear subcard is invalid."}; } consumed.push_back(card); }
    for (const auto &card : consumed) deck_.discard(card);
    ++slashUsedThisPhase_;
    deferredSlashDamage_.reset();
    pendingCardUse_ = CardUseContext {source->id(), "virtual-serpent-spear", CardType::Slash, {target->id()}, 1};
    pendingCard_.reset();
    log(source->name() + " used [Serpent Spear] to treat two hand cards as [Slash].");
    createDodgeRequest(*pendingCardUse_);
    return {true, "Virtual Slash is awaiting a Dodge response."};
}

ActionResult GameEngine::submitAction(const RespondVirtualSlashAction &action)
{
    if (!pendingResponse_ || action.requestId != pendingResponse_->requestId || action.playerId != pendingResponse_->responder) return {false, "Virtual Slash response request does not match."};
    const auto request = *pendingResponse_; auto responder = findPlayer(action.playerId);
    const bool slashResponse = request.type == ResponseType::Slash || request.type == ResponseType::BorrowedSwordSlash;
    if (!responder || !responder->isAlive() || !slashResponse || !weaponNamed(*responder, "丈八蛇矛") || action.subcardIds.size() != 2 || action.subcardIds[0] == action.subcardIds[1]) return {false, "Serpent Spear response is invalid."};
    if (request.type == ResponseType::BorrowedSwordSlash && (!borrowedSwordContext_ || !canUseKill(responder->id(), borrowedSwordContext_->attackTarget, true))) return {false, "Serpent Spear Slash cannot legally target the required player."};
    std::vector<std::shared_ptr<Card>> consumed;
    for (const auto &id : action.subcardIds) { auto card = responder->removeCard(id); if (!card) { for (const auto &returned : consumed) responder->addCard(returned); return {false, "Serpent Spear response subcard is invalid."}; } consumed.push_back(card); }
    for (const auto &card : consumed) deck_.discard(card);
    pendingResponse_.reset();
    log(responder->name() + " used [Serpent Spear] to treat two hand cards as a [Slash] response.");
    emitEvent(GameEventType::CardResponded, responder->id(), request.requester, "SerpentSpearSlash");
    if (multiTargetEffect_) {
        resolveMultiTargetResponse(true);
    } else if (request.type == ResponseType::BorrowedSwordSlash) {
        const auto context = *borrowedSwordContext_;
        log(responder->name() + " used [Serpent Spear] as [Slash] forced by [Borrowed Sword] targeting " + findPlayer(context.attackTarget)->name() + ".");
        pendingCardUse_ = CardUseContext {responder->id(), "virtual-serpent-spear", CardType::Slash, {context.attackTarget}, 1};
        createDodgeRequest(*pendingCardUse_);
    } else {
        resolvePendingDuel(true);
    }
    return {true, "Virtual Slash response accepted."};
}

ActionResult GameEngine::submitAction(const RespondAction &action)
{
    if (gameOver_) return {false, "The game is over."};
    if (!pendingResponse_) return {false, "There is no pending response."};
    const auto request = *pendingResponse_;
    if (action.requestId != request.requestId) return {false, "Response request ID does not match."};
    if (action.playerId != request.responder) return {false, "Only the requested responder may respond."};
    auto responder = findPlayer(action.playerId);
    if (!responder || (request.type == ResponseType::PeachRescue ? responder->status() == PlayerStatus::Dead : !responder->isAlive())) return {false, "Responder is not eligible."};
    if (request.type == ResponseType::PeachRescue) {
        if (action.cardId) {
            const auto card = findHandCard(*responder, *action.cardId);
            const bool peach = card && card->type() == CardType::Peach;
            const bool selfRescueWine = card && card->type() == CardType::Wine && dyingContext_
                && responder->id() == dyingContext_->dyingPlayer;
            if (!peach && !selfRescueWine) return {false, "A Peach, or the dying player's Wine, is required for rescue."};
        }
        resolvePeachRescue(request, action.cardId);
        return {true, "Rescue response handled."};
    }
    if (!action.cardId) {
        if (!request.allowDecline) return {false, "This response cannot be declined."};
        pendingResponse_.reset();
        if (request.type == ResponseType::Nullification) {
            resolveNullificationResponse(false);
        } else if (multiTargetEffect_) resolveMultiTargetResponse(false);
        else if (request.type == ResponseType::Dodge) {
            log(responder->name() + " declined to use [Dodge].");
            resolvePendingSlash(false);
        } else if (request.type == ResponseType::BorrowedSwordSlash) {
            resolveBorrowedSwordDecline();
        } else if (request.type == ResponseType::QinglongSlash) {
            qinglongContext_.reset();
            finishPendingCardUse();
        } else {
            log(responder->name() + " declined to use [Slash].");
            resolvePendingDuel(false);
        }
        return {true, "Response declined."};
    }
    auto card = findHandCard(*responder, *action.cardId);
    const CardType required = request.type == ResponseType::Dodge ? CardType::Dodge : request.type == ResponseType::Nullification ? CardType::Nullification : CardType::Slash;
    const bool slashResponse = request.type == ResponseType::Slash || request.type == ResponseType::BorrowedSwordSlash
        || request.type == ResponseType::QinglongSlash;
    if (!card || (slashResponse ? !isSlashCard(card->type()) : card->type() != required)) return {false, "Invalid response card."};
    if (request.type == ResponseType::BorrowedSwordSlash && (!borrowedSwordContext_ || !canUseKill(responder->id(), borrowedSwordContext_->attackTarget, true))) return {false, "Slash cannot legally target the required player."};
    if (request.type == ResponseType::QinglongSlash && (!qinglongContext_ || !weaponNamed(*responder, "青龙偃月刀") || !findPlayer(qinglongContext_->target) || !findPlayer(qinglongContext_->target)->isAlive())) return {false, "Qinglong follow-up is no longer legal."};
    deck_.discard(responder->removeCard(card->id()));
    pendingResponse_.reset();
    if (request.type == ResponseType::Nullification) {
        log(responder->name() + " used [Nullification]."); emitEvent(GameEventType::CardResponded, responder->id(), request.requester, "Nullification", card.get()); resolveNullificationResponse(true);
    } else if (multiTargetEffect_) {
        log(responder->name() + " used AOE response."); emitEvent(GameEventType::CardResponded, responder->id(), request.requester, request.type == ResponseType::Dodge ? "Dodge" : "Slash", card.get()); resolveMultiTargetResponse(true);
    } else if (request.type == ResponseType::Dodge) {
        log(responder->name() + " used [Dodge].");
        emitEvent(GameEventType::CardResponded, responder->id(), request.requester, "Dodge", card.get());
        resolvePendingSlash(true);
    } else if (request.type == ResponseType::BorrowedSwordSlash) {
        const auto context = *borrowedSwordContext_;
        log(responder->name() + " used [Slash] forced by [Borrowed Sword] targeting " + findPlayer(context.attackTarget)->name() + ".");
        emitEvent(GameEventType::CardResponded, responder->id(), context.attackTarget, "Slash", card.get());
        deferredSlashDamage_.reset();
        pendingCardUse_ = CardUseContext {responder->id(), card->id(), CardType::Slash, {context.attackTarget}, 1};
        createDodgeRequest(*pendingCardUse_);
    } else if (request.type == ResponseType::QinglongSlash) {
        const auto context = *qinglongContext_;
        qinglongContext_.reset();
        const auto nature = card->type() == CardType::FireSlash ? DamageNature::Fire : card->type() == CardType::ThunderSlash ? DamageNature::Thunder : DamageNature::Normal;
        log(responder->name() + " activated [Qinglong Blade] to use [Slash] again.");
        deferredSlashDamage_.reset();
        pendingCardUse_ = CardUseContext {responder->id(), card->id(), card->type(), {context.target}, 1, nature};
        createDodgeRequest(*pendingCardUse_);
    } else {
        log(responder->name() + " used [Slash] in [Duel].");
        emitEvent(GameEventType::CardResponded, responder->id(), request.requester, "Slash", card.get());
        resolvePendingDuel(true);
    }
    return {true, "Response accepted."};
}

ActionResult GameEngine::submitAction(const SelectCardsAction &action)
{
    if (gameOver_) return {false, "The game is over."};
    if (!pendingCardSelection_) return {false, "There is no pending card selection."};
    const auto request = *pendingCardSelection_;
    if (action.requestId != request.requestId) return {false, "Card selection request ID does not match."};
    if (action.playerId != request.requester) return {false, "Only the requesting player may select cards."};
    if (static_cast<int>(action.cardIds.size()) < request.minCount || static_cast<int>(action.cardIds.size()) > request.maxCount) {
        return {false, "Incorrect card selection count."};
    }
    if (std::set<CardId>(action.cardIds.begin(), action.cardIds.end()).size() != action.cardIds.size()) return {false, "Duplicate card selection."};
    if (request.purpose == CardSelectionPurpose::AxeDiscard) {
        if (!axeContext_ || axeContext_->requestId != request.requestId || axeContext_->source != action.playerId
            || !deferredSlashDamage_ || deferredSlashDamage_->source != axeContext_->source || deferredSlashDamage_->target != axeContext_->target) {
            return {false, "Axe request is no longer valid."};
        }
        const auto source = findPlayer(axeContext_->source);
        const auto target = findPlayer(axeContext_->target);
        if (!source || !target || !source->isAlive() || !target->isAlive() || !weaponNamed(*source, "贯石斧")) {
            cancelAxeContext(true);
            return {false, "Axe is no longer equipped."};
        }
        if (action.cardIds.empty()) { finishAxeDiscard(false); return {true, "Axe was declined."}; }
        if (action.cardIds.size() != 2) return {false, "Axe requires exactly two cards."};
        std::vector<std::optional<EquipmentSlot>> slots;
        slots.reserve(2);
        for (const auto& id : action.cardIds) {
            const auto allowed = std::find_if(request.selectableCards.begin(), request.selectableCards.end(), [&id](const SelectableCard& card) { return card.cardId == id; });
            if (allowed == request.selectableCards.end()) return {false, "Axe cost card is invalid."};
            if (allowed->zone == CardZone::Hand) {
                if (!findHandCard(*source, id)) return {false, "Axe hand cost is no longer owned."};
                slots.push_back(std::nullopt);
            } else {
                if (!allowed->equipmentSlot || !source->equipment(*allowed->equipmentSlot)
                    || source->equipment(*allowed->equipmentSlot)->id() != id) return {false, "Axe equipment cost is no longer owned."};
                slots.push_back(allowed->equipmentSlot);
            }
        }
        payingAxeCost_ = true;
        for (std::size_t index = 0; index < action.cardIds.size(); ++index) {
            std::shared_ptr<Card> discarded = slots[index] ? removeEquipment(*source, *slots[index]) : source->removeCard(action.cardIds[index]);
            deck_.discard(discarded);
        }
        payingAxeCost_ = false;
        finishAxeDiscard(true);
        return {true, "Axe damage resumed."};
    }
    if (request.purpose == CardSelectionPurpose::DoubleSwordDiscard) {
        if (!doubleSwordContext_ || doubleSwordContext_->requestId != request.requestId
            || doubleSwordContext_->target != action.playerId) {
            return {false, "Double Sword request is no longer valid."};
        }
        const auto source = findPlayer(doubleSwordContext_->source);
        const auto target = findPlayer(doubleSwordContext_->target);
        if (!source || !target || !source->isAlive() || !target->isAlive() || !weaponNamed(*source, "雌雄双股剑")) {
            cancelDoubleSwordContext(source && target && source->isAlive() && target->isAlive());
            return {false, "Double Sword players are no longer valid."};
        }
        if (action.cardIds.empty()) {
            finishDoubleSword(false);
            return {true, "Double Sword target allowed the attacker to draw."};
        }
        if (action.cardIds.size() != 1) return {false, "Double Sword requires exactly one hand card."};
        const auto& id = action.cardIds.front();
        const auto option = std::find_if(request.selectableCards.begin(), request.selectableCards.end(), [&id](const SelectableCard& card) {
            return card.cardId == id && card.zone == CardZone::Hand;
        });
        if (option == request.selectableCards.end() || !findHandCard(*target, id)) {
            return {false, "Double Sword hand card is no longer available."};
        }
        finishDoubleSword(true, id);
        return {true, "Double Sword target discarded a hand card."};
    }
    if (request.purpose == CardSelectionPurpose::IceSwordPrompt) {
        if (!iceSwordContext_ || iceSwordContext_->requestId != request.requestId || iceSwordContext_->source != action.playerId) return {false, "Ice Sword request is invalid."};
        if (action.cardIds.empty()) { finishIceSword(true); return {true, "Ice Sword was declined."}; }
        if (action.cardIds.size() != 1 || action.cardIds.front() != "ice-sword-activate") return {false, "Ice Sword activation is invalid."};
        deferredSlashDamage_.reset(); pendingCardSelection_.reset(); iceSwordContext_->activated = true; iceSwordContext_->discardsRemaining = 2; createIceSwordDiscardRequest();
        return {true, "Ice Sword prevented the Slash damage."};
    }
    if (request.purpose == CardSelectionPurpose::IceSwordDiscard) {
        if (!iceSwordContext_ || !iceSwordContext_->activated || iceSwordContext_->requestId != request.requestId || action.playerId != iceSwordContext_->source || action.cardIds.size() != 1) return {false, "Ice Sword discard is invalid."};
        const auto target = findPlayer(iceSwordContext_->target); const auto& id = action.cardIds.front();
        const auto option = std::find_if(request.selectableCards.begin(), request.selectableCards.end(), [&id](const auto& c){return c.cardId==id;});
        if (!target || option == request.selectableCards.end()) return {false, "Ice Sword card is invalid."};
        std::shared_ptr<Card> removed;
        if (option->zone == CardZone::Hand) removed = target->removeCard(id); else if (option->equipmentSlot) removed = removeEquipment(*target, *option->equipmentSlot);
        if (!removed || removed->id()!=id) return {false, "Ice Sword card is no longer available."};
        deck_.discard(removed); pendingCardSelection_.reset(); --iceSwordContext_->discardsRemaining;
        if (iceSwordContext_->discardsRemaining > 0) createIceSwordDiscardRequest(); else finishIceSword(false);
        return {true, "Ice Sword discarded a card."};
    }
    if (request.purpose == CardSelectionPurpose::KirinBowMount) {
        if (!kirinBowContext_ || kirinBowContext_->requestId != request.requestId || kirinBowContext_->source != action.playerId || gameOver_) return {false, "Kirin Bow request is no longer valid."};
        const auto source = findPlayer(kirinBowContext_->source); const auto target = findPlayer(kirinBowContext_->target);
        if (!source || !target || !source->isAlive() || !target->isAlive() || !weaponNamed(*source, "麒麟弓")) { kirinBowContext_.reset(); pendingCardSelection_.reset(); return {false, "Kirin Bow is no longer equipped."}; }
        if (action.cardIds.empty()) { finishKirinBow(); return {true, "Kirin Bow was declined."}; }
        if (action.cardIds.size() != 1) return {false, "Kirin Bow selects one mount."};
        const auto chosen = std::find_if(request.selectableCards.begin(), request.selectableCards.end(), [&action](const SelectableCard& c) { return c.cardId == action.cardIds.front(); });
        if (chosen == request.selectableCards.end() || !chosen->equipmentSlot || (*chosen->equipmentSlot != EquipmentSlot::OffensiveHorse && *chosen->equipmentSlot != EquipmentSlot::DefensiveHorse)) return {false, "Kirin Bow mount is invalid."};
        const auto mounted = target->equipment(*chosen->equipmentSlot);
        if (!mounted || mounted->id() != chosen->cardId) return {false, "Kirin Bow mount is no longer equipped."};
        const auto discarded = removeEquipment(*target, *chosen->equipmentSlot); deck_.discard(discarded);
        log(source->name() + " activated [Kirin Bow] and discarded " + target->name() + "'s [" + discarded->name() + "].");
        finishKirinBow(); return {true, "Kirin Bow mount discarded."};
    }
    if (request.purpose == CardSelectionPurpose::Harvest) {
        if (action.cardIds.size() != 1) return {false, "Harvest requires one card."};
        const auto selectable = std::find_if(request.selectableCards.begin(), request.selectableCards.end(), [&action](const SelectableCard &c) { return c.cardId == action.cardIds.front(); });
        if (selectable == request.selectableCards.end()) return {false, "Selected card is not in the Harvest pool."};
        resolveHarvestSelection(action.cardIds.front()); return {true, "Harvest selection completed."};
    }
    if (request.purpose == CardSelectionPurpose::FireAttackReveal) {
        if (action.cardIds.size() != 1 || !fireAttackContext_) return {false, "Fire Attack requires one revealed hand card."};
        const auto selectable = std::find_if(request.selectableCards.begin(), request.selectableCards.end(), [&action](const SelectableCard& card) { return card.cardId == action.cardIds.front(); });
        const auto target = findPlayer(request.target);
        const auto revealed = selectable == request.selectableCards.end() || !target ? nullptr : findHandCard(*target, selectable->cardId);
        if (!revealed) return {false, "Revealed card is no longer in hand."};
        fireAttackContext_->revealedCardId = selectable->cardId;
        pendingCardSelection_.reset();
        log(target->name() + " revealed [" + revealed->name() + "] " + suitDisplay(revealed->suit())
            + " " + std::to_string(revealed->rank()) + " for [Fire Attack].");
        createFireAttackDiscard();
        return {true, "Fire Attack card revealed."};
    }
    if (request.purpose == CardSelectionPurpose::FireAttackDiscard) {
        if (!fireAttackContext_) return {false, "Fire Attack context is missing."};
        if (action.cardIds.empty()) { fireAttackContext_.reset(); pendingCardSelection_.reset(); finishPendingCardUse(); return {true, "Fire Attack damage was declined."}; }
        const auto selectable = std::find_if(request.selectableCards.begin(), request.selectableCards.end(), [&action](const SelectableCard& card) { return card.cardId == action.cardIds.front(); });
        const auto source = findPlayer(request.requester);
        const auto target = findPlayer(fireAttackContext_->target);
        if (selectable == request.selectableCards.end() || !source || !target) return {false, "Fire Attack discard is invalid."};
        const auto discarded = source->removeCard(selectable->cardId);
        if (!discarded) return {false, "Fire Attack discard is no longer in hand."};
        deck_.discard(discarded);
        pendingCardSelection_.reset();
        const auto damageSource = fireAttackContext_->source;
        fireAttackContext_.reset();
        applyDamage(Damage {damageSource, target->id(), 1, DamageNature::Fire});
        if (!dyingContext_) finishPendingCardUse();
        return {true, "Fire Attack dealt Fire damage."};
    }
    auto target = findPlayer(request.target);
    if (!target) return {false, "Card selection target is invalid."};
    const auto selectable = std::find_if(request.selectableCards.begin(), request.selectableCards.end(), [&action](const SelectableCard &card) {
        return card.cardId == action.cardIds.front();
    });
    if (selectable == request.selectableCards.end()) return {false, "Selected card is not in the requested zone."};
    std::shared_ptr<Card> selected;
    if (selectable->zone == CardZone::Hand && request.allowHand) {
        selected = target->removeCard(selectable->cardId);
    } else if (selectable->zone == CardZone::Equipment && request.allowEquipment && selectable->equipmentSlot) {
        const auto equipped = target->equipment(*selectable->equipmentSlot);
        if (!equipped || equipped->id() != selectable->cardId) return {false, "Selected equipment is no longer available."};
        selected = removeEquipment(*target, *selectable->equipmentSlot);
    }
    const auto requester = findPlayer(request.requester);
    if (!selected || !requester) return {false, "Selected card is no longer available."};
    if (request.purpose == CardSelectionPurpose::Dismantlement) {
        deck_.discard(selected);
        log(selectable->zone == CardZone::Hand
            ? requester->name() + " discarded one hand card from " + target->name() + "."
            : requester->name() + " discarded equipped [" + selected->name() + "] from " + target->name() + ".");
    } else {
        requester->addCard(selected);
        log(selectable->zone == CardZone::Hand
            ? requester->name() + " obtained one hand card from " + target->name() + "."
            : requester->name() + " obtained equipped [" + selected->name() + "] from " + target->name() + ".");
    }
    const char* actionName = request.purpose == CardSelectionPurpose::Dismantlement ? "Dismantlement" : "Snatch";
    const char* zoneName = selectable->zone == CardZone::Hand ? "Hand"
        : selectable->zone == CardZone::Equipment ? "Equipment" : "Judgment";
    emitEvent(GameEventType::PublicCardMoved, requester->id(), target->id(),
              std::string(actionName) + ":" + zoneName + ":" + selected->name());
    pendingCardSelection_.reset();
    finishPendingCardUse();
    return {true, "Card selection completed."};
}

const std::vector<std::shared_ptr<Player>> &GameEngine::players() const noexcept { return players_; }
const Deck &GameEngine::deck() const noexcept { return deck_; }
const Player *GameEngine::player(PlayerId id) const noexcept { const auto found = findPlayer(id); return found.get(); }
const Player *GameEngine::currentPlayer() const noexcept { return currentPlayerIndex_ < players_.size() ? players_[currentPlayerIndex_].get() : nullptr; }
Phase GameEngine::currentPhase() const noexcept { return currentPhase_; }
int GameEngine::turnNumber() const noexcept { return turnNumber_; }
int GameEngine::requiredDiscardCount() const noexcept { const auto *p = currentPlayer(); return p ? std::max(0, static_cast<int>(p->handCards().size()) - p->hp()) : 0; }
int GameEngine::slashUsedThisPhase() const noexcept { return slashUsedThisPhase_; }
bool GameEngine::gameOver() const noexcept { return gameOver_; }
std::optional<PlayerId> GameEngine::winner() const noexcept { return winner_; }
WinningSide GameEngine::winningSide() const noexcept { return winningSide_; }
GameMode GameEngine::gameMode() const noexcept { return gameMode_; }
const std::vector<PlayerId>& GameEngine::winningPlayers() const noexcept { return winningPlayers_; }
std::optional<PlayerId> GameEngine::nextAlivePlayer(PlayerId from) const noexcept
{
    const auto fromIt = std::find_if(players_.begin(), players_.end(), [from](const auto& player) { return player->id() == from; });
    if (fromIt == players_.end() || players_.size() < 2) return std::nullopt;
    const auto start = static_cast<std::size_t>(std::distance(players_.begin(), fromIt));
    for (std::size_t offset = 1; offset < players_.size(); ++offset) {
        const auto& candidate = players_[(start + offset) % players_.size()];
        if (candidate->isAlive()) return candidate->id();
    }
    return std::nullopt;
}
const std::optional<ResponseRequest> &GameEngine::pendingResponse() const noexcept { return pendingResponse_; }
const std::optional<CardSelectionRequest> &GameEngine::pendingCardSelection() const noexcept { return pendingCardSelection_; }
const std::optional<DyingContext> &GameEngine::dyingContext() const noexcept { return dyingContext_; }
const std::optional<DuelContext> &GameEngine::duelContext() const noexcept { return duelContext_; }
const std::optional<QinglongContext> &GameEngine::qinglongContext() const noexcept { return qinglongContext_; }
const std::optional<AxeContext> &GameEngine::axeContext() const noexcept { return axeContext_; }
const std::optional<KirinBowContext> &GameEngine::kirinBowContext() const noexcept { return kirinBowContext_; }
const std::optional<IceSwordContext> &GameEngine::iceSwordContext() const noexcept { return iceSwordContext_; }
const std::optional<DoubleSwordContext> &GameEngine::doubleSwordContext() const noexcept { return doubleSwordContext_; }
const std::optional<MultiTargetEffectContext> &GameEngine::multiTargetEffect() const noexcept { return multiTargetEffect_; }
const std::optional<HarvestContext> &GameEngine::harvestContext() const noexcept { return harvestContext_; }
const std::optional<BorrowedSwordContext> &GameEngine::borrowedSwordContext() const noexcept { return borrowedSwordContext_; }
const std::optional<TrickResolutionContext> &GameEngine::trickResolution() const noexcept { return trickResolution_; }
const std::optional<NullificationChainContext> &GameEngine::nullificationChain() const noexcept { return nullificationChain_; }
const std::optional<ChainDamageContext> &GameEngine::chainDamageContext() const noexcept { return chainDamageContext_; }
const std::optional<FireAttackContext> &GameEngine::fireAttackContext() const noexcept { return fireAttackContext_; }
const std::optional<JudgmentContext> &GameEngine::judgmentContext() const noexcept { return judgmentContext_; }
const std::optional<JudgmentContext> &GameEngine::latestJudgment() const noexcept { return latestJudgment_; }
const std::vector<std::string> &GameEngine::logEntries() const noexcept { return logEntries_; }
const std::vector<EquipmentEffectEvent>& GameEngine::equipmentEffects() const noexcept { return equipmentEffects_; }
const std::vector<GameEvent> &GameEngine::eventHistory() const noexcept { return eventHistory_; }
void GameEngine::subscribeEvents(EventDispatcher::Listener listener) { dispatcher_.subscribe(std::move(listener)); }

bool GameEngine::canPlayCard(PlayerId id, const CardId &cardId) const
{
    if (actionBlocked() || !currentPlayer() || currentPlayer()->id() != id || currentPhase_ != Phase::Play) return false;
    const auto player = findPlayer(id); const auto card = player ? findHandCard(*player, cardId) : nullptr;
    if (!card) return false;
    if (isSlashCard(card->type())) {
        const auto weapon = player->equipment(EquipmentSlot::Weapon);
        return slashUsedThisPhase_ < 1 || (weapon && weapon->equipmentData() && weapon->equipmentData()->unlimitedSlash);
    }
    if (card->type() == CardType::Peach) return player->hp() < player->maxHp();
    if (card->type() == CardType::Wine) return !player->hasWineBuff();
    if (card->type() == CardType::ExNihilo || card->type() == CardType::Dismantlement
        || card->type() == CardType::Snatch || card->type() == CardType::Duel || card->type() == CardType::BarbarianInvasion || card->type() == CardType::ArrowBarrage || card->type() == CardType::PeachGarden || card->type() == CardType::Harvest || card->type() == CardType::BorrowedSword || card->type() == CardType::IronChain || card->type() == CardType::FireAttack || card->type() == CardType::Indulgence || card->type() == CardType::SupplyShortage || card->type() == CardType::Lightning) return true;
    if (equipmentSlotForCard(card->type())) return true;
    return false;
}

std::vector<PlayerId> GameEngine::virtualSlashTargets(PlayerId playerId) const
{
    std::vector<PlayerId> targets;
    const auto source = findPlayer(playerId);
    if (!source || !weaponNamed(*source, "丈八蛇矛") || actionBlocked()
        || !currentPlayer() || currentPlayer()->id() != playerId || currentPhase_ != Phase::Play) return targets;
    for (const auto& player : players_) {
        if (player && player->id() != playerId && player->isAlive() && canUseKill(playerId, player->id())) targets.push_back(player->id());
    }
    return targets;
}

std::vector<std::pair<PlayerId, PlayerId>> GameEngine::borrowedSwordTargetPairs(PlayerId playerId, const CardId& cardId) const
{
    std::vector<std::pair<PlayerId, PlayerId>> pairs;
    const auto source = findPlayer(playerId);
    const auto card = source ? findHandCard(*source, cardId) : nullptr;
    if (!card || card->type() != CardType::BorrowedSword || !canPlayCard(playerId, cardId)) return pairs;
    for (const auto& holder : players_) {
        if (!holder || !canPlayCardOnTarget(playerId, cardId, holder->id())) continue;
        for (const auto& target : players_) {
            if (target && target->id() != holder->id() && target->isAlive()
                && canUseKill(holder->id(), target->id(), true)) pairs.emplace_back(holder->id(), target->id());
        }
    }
    return pairs;
}

bool GameEngine::canPlayCardOnTarget(PlayerId playerId, const CardId &cardId, PlayerId targetId) const
{
    if (!canPlayCard(playerId, cardId)) return false;
    const auto source = findPlayer(playerId);
    const auto card = source ? findHandCard(*source, cardId) : nullptr;
    const auto target = findPlayer(targetId);
    if (!card || !target || !target->isAlive()) return false;
    switch (card->type()) {
    case CardType::Slash:
    case CardType::FireSlash:
    case CardType::ThunderSlash:
        return target->id() != source->id()
            && distanceBetween(source->id(), target->id()) <= attackRange(source->id());
    case CardType::Dismantlement:
        return target->id() != source->id() && hasSelectableCards(*target);
    case CardType::Snatch:
        return target->id() != source->id() && hasSelectableCards(*target)
            && distanceBetween(source->id(), target->id()) <= 1;
    case CardType::Duel:
        return target->id() != source->id();
    case CardType::BorrowedSword:
        return target->id() != source->id() && target->hasEquipment(EquipmentSlot::Weapon);
    case CardType::FireAttack:
        return target->id() != source->id() && !target->handCards().empty();
    case CardType::IronChain:
        return true;
    case CardType::Indulgence:
    case CardType::SupplyShortage:
    case CardType::Lightning:
        return target->id() != source->id() && !target->hasJudgmentCard(card->type());
    default:
        return false;
    }
}

int GameEngine::minTargetsForCard(PlayerId playerId, const CardId &cardId) const
{
    const auto source = findPlayer(playerId);
    const auto card = source ? findHandCard(*source, cardId) : nullptr;
    if (!source || !card) return 0;
    return card->type() == CardType::IronChain ? 0
        : (card->type() == CardType::FireAttack || card->type() == CardType::Indulgence
           || card->type() == CardType::SupplyShortage || card->type() == CardType::Lightning ? 1 : 0);
}

int GameEngine::maxTargetsForCard(PlayerId playerId, const CardId &cardId) const
{
    const auto source = findPlayer(playerId);
    const auto card = source ? findHandCard(*source, cardId) : nullptr;
    if (!source || !card) return 1;
    const bool slash = isSlashCard(card->type());
    if (slash && source->handCards().size() == 1 && weaponNamed(*source, "方天画戟")) return 3;
    if (card->type() == CardType::IronChain) return 2;
    return 1;
}

int GameEngine::distanceBetween(PlayerId source, PlayerId target) const
{
    const auto sourcePlayer = findPlayer(source);
    const auto targetPlayer = findPlayer(target);
    if (!sourcePlayer || !targetPlayer) return std::numeric_limits<int>::max();
    int distance = distanceCalculator_.distance(players_, source, target);
    if (distance == std::numeric_limits<int>::max()) return distance;
    if (sourcePlayer->hasEquipment(EquipmentSlot::OffensiveHorse)) --distance;
    if (targetPlayer->hasEquipment(EquipmentSlot::DefensiveHorse)) ++distance;
    return std::max(1, distance);
}

int GameEngine::attackRange(PlayerId playerId) const
{
    const auto player = findPlayer(playerId);
    if (!player) return 1;
    const auto weapon = player->equipment(EquipmentSlot::Weapon);
    if (!weapon || !weapon->equipmentData()) return 1;
    return std::max(1, weapon->equipmentData()->attackRange);
}

ActionResult GameEngine::handleTimeout()
{
    if (gameOver_) return {false, "The game is over."};
    if (pendingResponse_) {
        const auto request = *pendingResponse_;
        const auto player = findPlayer(request.responder);
        if (player) log(player->name() + " timed out while responding.");
        return submitAction(RespondAction {request.responder, request.requestId, std::nullopt});
    }
    if (pendingCardSelection_) {
        const auto& request = *pendingCardSelection_;
        if (const auto player = findPlayer(request.requester)) log(player->name() + " timed out while selecting a card.");
        if (request.purpose == CardSelectionPurpose::AxeDiscard) return submitAction(SelectCardsAction {request.requester, request.requestId, {}});
        if (request.purpose == CardSelectionPurpose::KirinBowMount) return submitAction(SelectCardsAction {request.requester, request.requestId, {}});
        if (request.purpose == CardSelectionPurpose::DoubleSwordDiscard) return submitAction(SelectCardsAction {request.requester, request.requestId, {}});
        if (request.purpose == CardSelectionPurpose::IceSwordPrompt) return submitAction(SelectCardsAction {request.requester, request.requestId, {}});
        if (request.selectableCards.empty()) { finishPendingCardUse(); return {true, "Empty selection timed out."}; }
        return submitAction(SelectCardsAction {request.requester, request.requestId, {request.selectableCards.front().cardId}});
    }
    if (!currentPlayer()) return {false, "No current player."};
    if (currentPhase_ == Phase::Play) { log(currentPlayer()->name() + " timed out in Play phase."); return submitAction(EndPlayPhaseAction {currentPlayer()->id()}); }
    if (currentPhase_ == Phase::Discard) {
        log(currentPlayer()->name() + " timed out in Discard phase.");
        std::vector<CardId> cards;
        const int count = requiredDiscardCount();
        for (int index = 0; index < count && index < static_cast<int>(currentPlayer()->handCards().size()); ++index) cards.push_back(currentPlayer()->handCards()[index]->id());
        return submitAction(DiscardAction {currentPlayer()->id(), cards});
    }
    return {false, "No timed interaction."};
}
#ifdef SANGUOSHA_TESTING
void GameEngine::testingSetHp(PlayerId playerId, int hp)
{
    auto player = findPlayer(playerId);
    if (!player) return;
    while (player->hp() > hp) player->receiveDamage(1);
    while (player->hp() < hp) player->recoverHp(1);
}
void GameEngine::testingSetPlayerStatus(PlayerId playerId, PlayerStatus status)
{
    if (const auto player = findPlayer(playerId)) player->setStatus(status);
}
void GameEngine::testingSetChained(PlayerId playerId, bool chained)
{
    if (const auto player = findPlayer(playerId)) player->setChained(chained);
}
void GameEngine::testingSetIgnoreArmorForNextSlash(bool enabled) { testingIgnoreArmorForNextSlash_ = enabled; }
void GameEngine::testingCheckGameOver() { checkGameOver(); }
void GameEngine::testingKillPlayer(PlayerId playerId) { killPlayer(playerId); }
void GameEngine::testingEquip(PlayerId playerId, const std::shared_ptr<Card>& card)
{
    if (const auto player = findPlayer(playerId)) {
        const bool losesDoubleSword = doubleSwordContext_ && doubleSwordContext_->source == playerId
            && weaponNamed(*player, "雌雄双股剑");
        if (iceSwordContext_ && iceSwordContext_->source == playerId && !iceSwordContext_->activated
            && weaponNamed(*player, "寒冰剑")) {
            finishIceSword(true);
        }
        player->equip(card);
        if (losesDoubleSword) cancelDoubleSwordContext(true);
        if (axeContext_ && axeContext_->source == playerId && !weaponNamed(*player, "贯石斧")) cancelAxeContext(true);
    }
}
void GameEngine::testingRemoveEquipment(PlayerId playerId, EquipmentSlot slot)
{
    if (const auto player = findPlayer(playerId)) removeEquipment(*player, slot);
}
void GameEngine::testingSetIdentity(PlayerId playerId, PlayerIdentity identity)
{
    if (const auto player = findPlayer(playerId)) player->setIdentity(identity);
}
void GameEngine::testingSetGender(PlayerId playerId, Gender gender)
{
    if (const auto player = findPlayer(playerId)) player->setGender(gender);
}
void GameEngine::testingAddToDrawPile(const std::shared_ptr<Card>& card)
{
    deck_.addToDrawPile(card);
}
void GameEngine::testingSetRandomSeed(std::uint32_t seed)
{
    random_ = RandomGenerator(seed);
}
std::shared_ptr<Card> GameEngine::testingDrawFromDeck()
{
    return deck_.drawCard(random_);
}
void GameEngine::testingStartSlash(PlayerId source, const std::vector<PlayerId>& targets, int baseDamage,
                                   DamageNature nature, bool ignoreArmor)
{
    if (targets.empty()) return;
    deferredSlashDamage_.reset();
    pendingCard_.reset();
    pendingCardUse_ = CardUseContext {source, "testing-slash", CardType::Slash, targets, baseDamage, nature, ignoreArmor};
    createDodgeRequest(*pendingCardUse_);
}
const std::optional<SlashDamageSnapshot>& GameEngine::testingDeferredSlashDamage() const noexcept
{
    return deferredSlashDamage_;
}
bool GameEngine::testingResumeDeferredSlashDamage()
{
    if (axeContext_) {
        finishAxeDiscard(true);
        return true;
    }
    return resumeDeferredSlashDamage();
}
void GameEngine::testingApplyDamage(const Damage& damage) { applyDamage(damage); }
#endif

void GameEngine::beginTurn()
{
    skipDrawThisTurn_ = false;
    skipPlayThisTurn_ = false;
    judgmentContext_.reset();
    ++turnNumber_; log("Turn " + std::to_string(turnNumber_) + " started: " + currentPlayer()->name() + ".");
    emitEvent(GameEventType::TurnStarted, currentPlayer()->id()); enterPhase(Phase::Start);
}

void GameEngine::enterPhase(Phase phase)
{
    if (gameOver_) return;
    if (started_) emitEvent(GameEventType::PhaseEnded, currentPlayer()->id(), {}, std::to_string(static_cast<int>(currentPhase_)));
    currentPhase_ = phase; if (phase == Phase::Play) slashUsedThisPhase_ = 0;
    const char *names[] = {"Start", "Judge", "Draw", "Play", "Discard", "Finish"};
    log(currentPlayer()->name() + " entered " + names[static_cast<int>(phase)] + " phase.");
    emitEvent(GameEventType::PhaseStarted, currentPlayer()->id(), {}, names[static_cast<int>(phase)]);
    switch (phase) {
    case Phase::Start: enterPhase(Phase::Judge); break;
    case Phase::Judge: resolveJudgmentPhase(); break;
    case Phase::Draw:
        if (skipDrawThisTurn_) log(currentPlayer()->name() + " skipped Draw phase.");
        else drawCards(currentPlayer()->id(), 2);
        enterPhase(Phase::Play);
        break;
    case Phase::Play:
        if (skipPlayThisTurn_) { log(currentPlayer()->name() + " skipped Play phase."); enterPhase(Phase::Discard); }
        break;
    case Phase::Discard: if (requiredDiscardCount() == 0) enterPhase(Phase::Finish); break;
    case Phase::Finish: completeTurn(); break;
    }
}

void GameEngine::resolveJudgmentPhase()
{
    const auto player = currentPlayer() ? findPlayer(currentPlayer()->id()) : nullptr;
    if (!player) return;
    JudgmentPhaseContext context {player->id(), {}, 0, {}};
    for (const auto &trick : player->judgmentCards()) if (trick) context.delayedTrickIds.push_back(trick->id());
    judgmentPhaseContext_ = std::move(context);
    continueJudgmentPhase();
}

void GameEngine::continueJudgmentPhase()
{
    if (!judgmentPhaseContext_) { enterPhase(Phase::Draw); return; }
    auto& phase = *judgmentPhaseContext_;
    const auto player = findPlayer(phase.player);
    if (!player) { judgmentPhaseContext_.reset(); enterPhase(Phase::Draw); return; }
    while (phase.currentIndex < phase.delayedTrickIds.size()) {
        auto delayed = player->removeJudgmentCard(phase.delayedTrickIds[phase.currentIndex++]);
        if (!delayed) continue;
        phase.delayedTrick = delayed;
        const auto sourceIt = delayedTrickSources_.find(delayed->id());
        const auto source = sourceIt != delayedTrickSources_.end() ? sourceIt->second : player->id();
        beginTrickResolution(source, delayed->id(), delayed->type(), player->id());
        return;
    }
    judgmentPhaseContext_.reset();
    enterPhase(Phase::Draw);
}

void GameEngine::resolveCurrentDelayedTrickJudgment()
{
    if (!judgmentPhaseContext_ || !judgmentPhaseContext_->delayedTrick) { continueJudgmentPhase(); return; }
    auto& phase = *judgmentPhaseContext_;
    const auto player = findPlayer(phase.player);
    auto delayed = std::move(phase.delayedTrick);
    if (!player || !delayed) { continueJudgmentPhase(); return; }
    delayedTrickSources_.erase(delayed->id());
    const auto judgment = deck_.drawCard(random_);
    if (!judgment) { player->addJudgmentCard(delayed); judgmentPhaseContext_.reset(); log("No card available for judgment."); enterPhase(Phase::Draw); return; }
    JudgmentContext context {player->id(), delayed->id(), delayed->type(), judgment, false, phase.currentIndex - 1};
    const char* suits[] = {"Spade", "Heart", "Club", "Diamond"};
    log(player->name() + " judged [" + judgment->name() + "] "
        + suits[static_cast<int>(judgment->suit())] + " " + std::to_string(judgment->rank()) + ".");
    switch (delayed->type()) {
    case CardType::Indulgence:
        context.succeeded = judgment->suit() == Suit::Heart;
        if (!context.succeeded) skipPlayThisTurn_ = true;
        deck_.discard(delayed);
        log(player->name() + (context.succeeded ? " passed [Indulgence] judgment." : " failed [Indulgence] judgment and skips Play phase."));
        break;
    case CardType::SupplyShortage:
        context.succeeded = judgment->suit() == Suit::Club;
        if (!context.succeeded) skipDrawThisTurn_ = true;
        deck_.discard(delayed);
        log(player->name() + (context.succeeded ? " passed [Supply Shortage] judgment." : " failed [Supply Shortage] judgment and skips Draw phase."));
        break;
    case CardType::Lightning: {
        context.succeeded = judgment->suit() == Suit::Spade && judgment->rank() >= 2 && judgment->rank() <= 9;
        if (context.succeeded) {
            deck_.discard(delayed); judgmentContext_ = context; latestJudgment_ = context; deck_.discard(judgment); judgmentContext_.reset();
            applyDamage(Damage {player->id(), player->id(), 3, DamageNature::Thunder});
            log(player->name() + " was struck by [Lightning].");
            if (gameOver_ || dyingContext_) return;
            continueJudgmentPhase(); return;
        }
        std::optional<PlayerId> recipient = nextAlivePlayer(player->id());
        while (recipient && *recipient != player->id() && findPlayer(*recipient)->hasJudgmentCard(CardType::Lightning)) recipient = nextAlivePlayer(*recipient);
        if (recipient && *recipient != player->id()) { findPlayer(*recipient)->addJudgmentCard(delayed); log("[Lightning] passed to " + findPlayer(*recipient)->name() + "."); }
        else deck_.discard(delayed);
        break;
    }
    default: deck_.discard(delayed); break;
    }
    judgmentContext_ = context; latestJudgment_ = context; deck_.discard(judgment); judgmentContext_.reset();
    if (gameOver_ || dyingContext_) return;
    continueJudgmentPhase();
}

void GameEngine::drawCards(PlayerId id, int count)
{
    auto player = findPlayer(id); if (!player || count <= 0) return;
    auto cards = deck_.drawCards(count, random_); player->addCards(cards);
    log(player->name() + " drew " + std::to_string(cards.size()) + " card(s).");
}

void GameEngine::completeTurn()
{
    auto endingPlayer = findPlayer(currentPlayer()->id());
    if (endingPlayer->hasWineBuff()) {
        endingPlayer->setWineBuff(false);
        log(endingPlayer->name() + "'s [Wine] effect expired at turn end.");
    }
    skipDrawThisTurn_ = false;
    skipPlayThisTurn_ = false;
    judgmentContext_.reset();
    log("Turn " + std::to_string(turnNumber_) + " ended."); emitEvent(GameEventType::TurnEnded, currentPlayer()->id());
    if (const auto nextId = nextAlivePlayer(currentPlayer()->id())) {
        const auto next = std::find_if(players_.begin(), players_.end(), [nextId](const auto& player) { return player->id() == *nextId; });
        currentPlayerIndex_ = static_cast<std::size_t>(std::distance(players_.begin(), next));
        beginTurn();
    }
}

void GameEngine::createDodgeRequest(const CardUseContext &context)
{
    const PlayerId target = context.targets.at(context.currentTargetIndex);
    if (createDoubleSwordRequest(context)) return;
    if (const auto player = findPlayer(target); player && armorApplies(*player, context, "八卦阵")) {
        log(player->name() + " triggered [Bagua].");
        const auto judgment = deck_.drawCard(random_);
        if (judgment) {
            const bool success = judgment->suit() == Suit::Heart || judgment->suit() == Suit::Diamond;
            latestJudgment_ = JudgmentContext {target, "bagua", CardType::Armor, judgment, success, 0};
            const char* suits[] = {"Spade", "Heart", "Club", "Diamond"};
            log(player->name() + " judged [" + judgment->name() + "] " + suits[static_cast<int>(judgment->suit())]
                + " " + std::to_string(judgment->rank()) + ".");
            deck_.discard(judgment);
            log(player->name() + (success ? " judged red: [Bagua] succeeded and counts as [Dodge]."
                                         : " judged black: [Bagua] failed."));
            if (success) { resolvePendingSlash(true); return; }
        }
    }
    pendingResponse_ = ResponseRequest {nextRequestId_++, ResponseType::Dodge, context.source, target, context.cardId, true};
    log("Waiting for " + findPlayer(target)->name() + " to respond with [Dodge].");
    notifyInteractionCreated();
}

bool GameEngine::createDoubleSwordRequest(const CardUseContext& context)
{
    if (!pendingCardUse_ || pendingCardUse_->currentTargetIndex >= pendingCardUse_->targets.size()) return false;
    const PlayerId targetId = pendingCardUse_->targets[pendingCardUse_->currentTargetIndex];
    if (std::find(pendingCardUse_->doubleSwordResolvedTargets.begin(), pendingCardUse_->doubleSwordResolvedTargets.end(), targetId)
        != pendingCardUse_->doubleSwordResolvedTargets.end()) return false;
    const auto source = findPlayer(context.source);
    const auto target = findPlayer(targetId);
    if (!source || !target || !source->isAlive() || !target->isAlive()
        || !weaponNamed(*source, "雌雄双股剑")
        || source->gender() == Gender::Unknown || target->gender() == Gender::Unknown
        || source->gender() == target->gender()) return false;

    pendingCardUse_->doubleSwordResolvedTargets.push_back(targetId);
    if (target->handCards().empty()) {
        drawCards(source->id(), 1);
        log(source->name() + " drew a card with [Double Sword] because " + target->name() + " had no hand cards.");
        return false;
    }
    const auto requestId = nextRequestId_++;
    CardSelectionRequest request {requestId, CardSelectionPurpose::DoubleSwordDiscard, target->id(), target->id(), 0, 1, true, false, {}};
    for (const auto& card : target->handCards()) request.selectableCards.push_back({card->id(), CardZone::Hand, std::nullopt, false, card->name()});
    doubleSwordContext_ = DoubleSwordContext {source->id(), target->id(), requestId};
    pendingCardSelection_ = std::move(request);
    log(target->name() + " may discard one hand card for [Double Sword], otherwise " + source->name() + " draws a card.");
    notifyInteractionCreated();
    return true;
}

void GameEngine::finishDoubleSword(bool targetDiscarded, const std::optional<CardId>& discardedCard)
{
    const auto context = doubleSwordContext_;
    pendingCardSelection_.reset();
    doubleSwordContext_.reset();
    if (!context) return;
    const auto source = findPlayer(context->source);
    const auto target = findPlayer(context->target);
    if (targetDiscarded && target && discardedCard) {
        if (const auto discarded = target->removeCard(*discardedCard)) deck_.discard(discarded);
    } else if (source && source->isAlive()) {
        drawCards(source->id(), 1);
    }
    if (!gameOver_ && !dyingContext_ && pendingCardUse_) createDodgeRequest(*pendingCardUse_);
}

void GameEngine::cancelDoubleSwordContext(bool continueSlash)
{
    if (!doubleSwordContext_) return;
    pendingCardSelection_.reset();
    doubleSwordContext_.reset();
    if (continueSlash && !gameOver_ && !dyingContext_ && pendingCardUse_) createDodgeRequest(*pendingCardUse_);
}

bool GameEngine::armorApplies(const Player &player, const CardUseContext &context, const char *armorName) const
{
    const auto armor = player.equipment(EquipmentSlot::Armor);
    return !context.ignoreArmor && armor && armor->name() == armorName;
}
bool GameEngine::vineArmorApplies(const Player &player, const CardUseContext &context) const
{
    return armorApplies(player, context, "藤甲");
}

void GameEngine::createDuelRequest()
{
    if (!duelContext_) return;
    const auto &context = *duelContext_;
    const PlayerId requester = context.currentResponder == context.source ? context.target : context.source;
    pendingResponse_ = ResponseRequest {nextRequestId_++, ResponseType::Slash, requester, context.currentResponder,
        context.duelCardId, true};
    log("Waiting for " + findPlayer(context.currentResponder)->name() + " to respond with [Slash] in [Duel].");
    notifyInteractionCreated();
}

void GameEngine::startMultiTargetEffect(MultiTargetEffectType type, PlayerId source, const CardId& cardId)
{
    const auto sourceIt = std::find_if(players_.begin(), players_.end(), [source](const auto& p) { return p->id() == source; });
    if (sourceIt == players_.end()) { finishPendingCardUse(); return; }
    MultiTargetEffectContext context {type, source, cardId, {}, 0, type == MultiTargetEffectType::BarbarianInvasion ? ResponseType::Slash : ResponseType::Dodge, false};
    const auto start = static_cast<std::size_t>(std::distance(players_.begin(), sourceIt));
    for (std::size_t offset = 1; offset < players_.size(); ++offset) { const auto& p = players_[(start + offset) % players_.size()]; if (p->isAlive()) context.orderedTargets.push_back(p->id()); }
    multiTargetEffect_ = std::move(context); createMultiTargetResponse();
}

void GameEngine::createMultiTargetResponse()
{
    if (!multiTargetEffect_ || gameOver_) { if (gameOver_) finishPendingCardUse(); return; }
    auto& context = *multiTargetEffect_;
    while (context.currentTargetIndex < context.orderedTargets.size()) {
        const auto responder = context.orderedTargets[context.currentTargetIndex]; const auto p = findPlayer(responder);
        if (p && p->isAlive() && vineArmorApplies(*p, CardUseContext {context.source, context.cardId, CardType::Slash, {responder}})) {
            emitEquipmentEffect("藤甲", EquipmentEffectType::DamagePrevented, p->id(), p->id(), 0, context.type == MultiTargetEffectType::BarbarianInvasion ? CardType::BarbarianInvasion : CardType::ArrowBarrage);
            log(p->name() + " was protected by [Vine Armor]."); ++context.currentTargetIndex; continue;
        }
        if (p && p->isAlive()) {
            if (!context.awaitingNullification) {
                context.awaitingNullification = true;
                beginTrickResolution(context.source, context.cardId,
                    context.type == MultiTargetEffectType::BarbarianInvasion ? CardType::BarbarianInvasion : CardType::ArrowBarrage, responder);
                return;
            }
            pendingResponse_ = ResponseRequest {nextRequestId_++, context.requiredResponse, context.source, responder, context.cardId, true}; log("Waiting for " + p->name() + " to respond to AOE."); notifyInteractionCreated(); return;
        }
        ++context.currentTargetIndex;
    }
    finishPendingCardUse();
}

void GameEngine::resolveMultiTargetResponse(bool responded)
{
    if (!multiTargetEffect_) return;
    const auto context = *multiTargetEffect_; if (context.currentTargetIndex >= context.orderedTargets.size()) { finishPendingCardUse(); return; }
    const auto target = context.orderedTargets[context.currentTargetIndex]; ++multiTargetEffect_->currentTargetIndex; multiTargetEffect_->awaitingNullification = false;
    if (!responded) { log(findPlayer(target)->name() + " failed to respond to AOE."); applyDamage(Damage {context.source, target, 1}); if (gameOver_ || dyingContext_) return; }
    createMultiTargetResponse();
}

void GameEngine::resolvePeachGarden(PlayerId source)
{
    for (const auto& p : players_) if (!gameOver_ && p->isAlive() && p->hp() < p->maxHp()) applyRecover(Recover {source, p->id(), 1});
}

void GameEngine::startHarvest(PlayerId source)
{
    HarvestContext context {source, {}, 0, {}, 0, false, {}};
    const auto sourceIt = std::find_if(players_.begin(), players_.end(), [source](const auto& p) { return p->id() == source; });
    if (sourceIt == players_.end()) { finishPendingCardUse(); return; }
    const auto start = static_cast<std::size_t>(std::distance(players_.begin(), sourceIt));
    for (std::size_t offset = 0; offset < players_.size(); ++offset) { const auto& p = players_[(start + offset) % players_.size()]; if (p->isAlive()) context.targetOrder.push_back(p->id()); }
    context.pool = deck_.drawCards(static_cast<int>(context.targetOrder.size()), random_);
    log("[Harvest] revealed " + std::to_string(context.pool.size()) + " card(s).");
    harvestContext_ = std::move(context); createHarvestSelection();
}

void GameEngine::createHarvestSelection()
{
    if (!harvestContext_ || gameOver_) { if (gameOver_) finishHarvest(); return; }
    auto& context = *harvestContext_;
    while (context.currentTargetIndex < context.targetOrder.size()) {
        const auto picker = findPlayer(context.targetOrder[context.currentTargetIndex]);
        if (picker && picker->isAlive() && !context.pool.empty()) {
            if (!context.awaitingNullification) {
                context.awaitingNullification = true;
                beginTrickResolution(context.source, pendingCardUse_ ? pendingCardUse_->cardId : "harvest", CardType::Harvest, picker->id());
                return;
            }
            createHarvestCardSelection(); return;
        }
        ++context.currentTargetIndex;
    }
    finishHarvest();
}

void GameEngine::createHarvestCardSelection()
{
    if (!harvestContext_) return;
    auto& context = *harvestContext_;
    if (context.currentTargetIndex >= context.targetOrder.size()) { finishHarvest(); return; }
    const auto picker = findPlayer(context.targetOrder[context.currentTargetIndex]);
    if (!picker || !picker->isAlive() || context.pool.empty()) { ++context.currentTargetIndex; context.awaitingNullification = false; createHarvestSelection(); return; }
    CardSelectionRequest request {nextRequestId_++, CardSelectionPurpose::Harvest, picker->id(), picker->id(), 1, 1, false, false, {}};
    for (const auto& card : context.pool) request.selectableCards.push_back({card->id(), CardZone::Hand, std::nullopt, false, card->name()});
    context.currentRequestId = request.requestId; pendingCardSelection_ = std::move(request);
    log("Waiting for " + picker->name() + " to choose a [Harvest] card.");
    notifyInteractionCreated();
}

void GameEngine::resolveHarvestSelection(const CardId& cardId)
{
    if (!harvestContext_ || !pendingCardSelection_) return;
    auto& context = *harvestContext_; const auto picker = findPlayer(pendingCardSelection_->requester);
    const auto it = std::find_if(context.pool.begin(), context.pool.end(), [&cardId](const auto& card) { return card->id() == cardId; });
    if (it == context.pool.end() || !picker || !picker->isAlive()) return;
    auto selected = *it;
    context.pool.erase(it);
    context.choices.push_back(HarvestChoice {picker->id(), selected});
    picker->addCard(selected);
    pendingCardSelection_.reset();
    log(picker->name() + " obtained [" + selected->name() + "] " + suitDisplay(selected->suit())
        + " " + std::to_string(selected->rank()) + " from [Harvest].");
    ++context.currentTargetIndex;
    context.awaitingNullification = false;
    createHarvestSelection();
}

void GameEngine::finishHarvest()
{
    if (harvestContext_) for (const auto& card : harvestContext_->pool) deck_.discard(card);
    harvestContext_.reset(); pendingCardSelection_.reset(); finishPendingCardUse();
}

bool GameEngine::canUseKill(PlayerId sourceId, PlayerId targetId, bool ignorePhaseLimit) const
{
    const auto source = findPlayer(sourceId); const auto target = findPlayer(targetId);
    if (!source || !target || !source->isAlive() || !target->isAlive() || sourceId == targetId) return false;
    if (!ignorePhaseLimit && slashUsedThisPhase_ >= 1) { const auto weapon = source->equipment(EquipmentSlot::Weapon); if (!weapon || !weapon->equipmentData() || !weapon->equipmentData()->unlimitedSlash) return false; }
    return distanceBetween(sourceId, targetId) <= attackRange(sourceId);
}

void GameEngine::startBorrowedSword(PlayerId source, PlayerId holder, PlayerId target)
{
    const auto weapon = findPlayer(holder)->equipment(EquipmentSlot::Weapon);
    borrowedSwordContext_ = BorrowedSwordContext {source, holder, target, weapon->id(), 0}; createBorrowedSwordResponse();
}

void GameEngine::createBorrowedSwordResponse()
{
    if (!borrowedSwordContext_ || gameOver_) { if (gameOver_) finishBorrowedSword(); return; }
    auto& context = *borrowedSwordContext_; const auto holder = findPlayer(context.weaponHolder); const auto target = findPlayer(context.attackTarget);
    if (!holder || !target || !holder->isAlive() || !target->isAlive() || !holder->equipment(EquipmentSlot::Weapon) || holder->equipment(EquipmentSlot::Weapon)->id() != context.weaponCardId) { resolveBorrowedSwordDecline(); return; }
    pendingResponse_ = ResponseRequest {nextRequestId_++, ResponseType::BorrowedSwordSlash, context.source, context.weaponHolder, pendingCardUse_ ? pendingCardUse_->cardId : "borrowed-sword", true};
    context.requestId = pendingResponse_->requestId; log("Waiting for " + holder->name() + " to use [Slash] forced by [Borrowed Sword].");
    notifyInteractionCreated();
}

void GameEngine::resolveBorrowedSwordDecline()
{
    if (!borrowedSwordContext_) return;
    const auto context = *borrowedSwordContext_; pendingResponse_.reset(); const auto source = findPlayer(context.source); const auto holder = findPlayer(context.weaponHolder);
    if (source && holder && source->isAlive() && holder->equipment(EquipmentSlot::Weapon) && holder->equipment(EquipmentSlot::Weapon)->id() == context.weaponCardId) {
        auto weapon = holder->removeEquipment(EquipmentSlot::Weapon); source->addCard(weapon); log(holder->name() + " gave [" + weapon->name() + "] to " + source->name() + " after declining [Borrowed Sword].");
    }
    finishBorrowedSword();
}

void GameEngine::finishBorrowedSword()
{
    borrowedSwordContext_.reset(); finishPendingCardUse();
}

void GameEngine::beginTrickResolution(PlayerId source, const CardId& cardId, CardType type, std::optional<PlayerId> target)
{
    trickResolution_ = TrickResolutionContext {source, cardId, type, target};
    NullificationChainContext chain; chain.roundStartAfter = source; chain.initialRound = true;
    const auto sourceIt = std::find_if(players_.begin(), players_.end(), [source](const auto& p) { return p->id() == source; });
    if (sourceIt == players_.end()) { finishNullificationChain(); return; }
    const auto start = static_cast<std::size_t>(std::distance(players_.begin(), sourceIt));
    for (std::size_t offset = 1; offset < players_.size(); ++offset) { const auto& p = players_[(start + offset) % players_.size()]; if (p->isAlive()) chain.responderOrder.push_back(p->id()); }
    nullificationChain_ = std::move(chain); createNullificationResponse();
}

void GameEngine::createNullificationResponse()
{
    if (!nullificationChain_ || gameOver_) { if (gameOver_) finishPendingCardUse(); return; }
    auto& chain = *nullificationChain_;
    while (chain.currentResponderIndex < chain.responderOrder.size()) {
        const auto responder = findPlayer(chain.responderOrder[chain.currentResponderIndex]);
        if (responder && responder->isAlive()) {
            // Eligibility is public rule state (turn order and life state), not
            // the responder's hidden hand.  An empty private legal-card list
            // simply makes Pass the only valid response.
            pendingResponse_ = ResponseRequest {nextRequestId_++, ResponseType::Nullification, trickResolution_->source, responder->id(), trickResolution_->cardId, true};
            log("Waiting for " + responder->name() + " to respond with [Nullification].");
            const bool beneficial = trickResolution_->cardType == CardType::ExNihilo
                || trickResolution_->cardType == CardType::PeachGarden || trickResolution_->cardType == CardType::Harvest;
            emitEvent(GameEventType::NullificationContext, trickResolution_->source, trickResolution_->effectTarget,
                      std::string(beneficial ? "Beneficial:" : "Harmful:") + std::to_string(chain.nullificationCount));
            notifyInteractionCreated();
            return;
        }
        ++chain.currentResponderIndex;
    }
    finishNullificationChain();
}

void GameEngine::resolveNullificationResponse(bool played)
{
    if (!nullificationChain_) return;
    auto& chain = *nullificationChain_; const auto responder = chain.responderOrder[chain.currentResponderIndex]; pendingResponse_.reset();
    if (played) {
        ++chain.nullificationCount; chain.roundStartAfter = responder; chain.initialRound = false; chain.responderOrder.clear(); chain.currentResponderIndex = 0;
        const auto it = std::find_if(players_.begin(), players_.end(), [responder](const auto& p) { return p->id() == responder; });
        const auto start = static_cast<std::size_t>(std::distance(players_.begin(), it));
        for (std::size_t offset = 1; offset <= players_.size(); ++offset) { const auto& p = players_[(start + offset) % players_.size()]; if (p->isAlive()) chain.responderOrder.push_back(p->id()); }
    } else ++chain.currentResponderIndex;
    createNullificationResponse();
}

void GameEngine::finishNullificationChain()
{
    const bool resolves = nullificationChain_ && nullificationChain_->nullificationCount % 2 == 0;
    const auto trick = trickResolution_; nullificationChain_.reset(); trickResolution_.reset();
    if (!trick) { finishPendingCardUse(); return; }
    if (judgmentPhaseContext_ && judgmentPhaseContext_->delayedTrick && trick->cardId == judgmentPhaseContext_->delayedTrick->id()) {
        if (!resolves) { delayedTrickSources_.erase(judgmentPhaseContext_->delayedTrick->id()); deck_.discard(judgmentPhaseContext_->delayedTrick); judgmentPhaseContext_->delayedTrick.reset(); log("[Trick] was nullified before judgment."); continueJudgmentPhase(); }
        else resolveCurrentDelayedTrickJudgment();
        return;
    }
    if (trick->cardType == CardType::Harvest && harvestContext_) {
        auto& harvest = *harvestContext_;
        harvest.awaitingNullification = false;
        if (!resolves) { log("[Harvest] was nullified for current recipient."); ++harvest.currentTargetIndex; createHarvestSelection(); }
        else createHarvestCardSelection();
        return;
    }
    if ((trick->cardType == CardType::BarbarianInvasion || trick->cardType == CardType::ArrowBarrage) && multiTargetEffect_) {
        auto& aoe = *multiTargetEffect_;
        if (!resolves) { log("[AOE] was nullified for remaining targets."); finishPendingCardUse(); }
        else createMultiTargetResponse();
        return;
    }
    if (!resolves) { log("[Trick] was nullified."); finishPendingCardUse(); return; }
    if (trick->cardType == CardType::ExNihilo) { log("[Ex Nihilo] resolved."); drawCards(trick->source, 2); }
    else if (trick->cardType == CardType::PeachGarden) { resolvePeachGarden(trick->source); }
    else if (trick->cardType == CardType::Harvest) { startHarvest(trick->source); return; }
    else if (trick->cardType == CardType::Dismantlement || trick->cardType == CardType::Snatch) {
        if (trick->effectTarget) { createCardSelectionRequest(trick->cardType == CardType::Dismantlement ? CardSelectionPurpose::Dismantlement : CardSelectionPurpose::Snatch, trick->source, *trick->effectTarget); return; }
    }
    else if (trick->cardType == CardType::Duel) {
        if (trick->effectTarget) { duelContext_ = DuelContext {trick->source, *trick->effectTarget, *trick->effectTarget, trick->cardId}; createDuelRequest(); return; }
    }
    else if (trick->cardType == CardType::BorrowedSword && pendingCardUse_ && pendingCardUse_->targets.size() == 2) {
        startBorrowedSword(trick->source, pendingCardUse_->targets[0], pendingCardUse_->targets[1]); return;
    }
    else if (trick->cardType == CardType::BarbarianInvasion || trick->cardType == CardType::ArrowBarrage) {
        startMultiTargetEffect(trick->cardType == CardType::BarbarianInvasion ? MultiTargetEffectType::BarbarianInvasion : MultiTargetEffectType::ArrowBarrage, trick->source, trick->cardId); return;
    }
    else if (trick->cardType == CardType::Indulgence || trick->cardType == CardType::SupplyShortage || trick->cardType == CardType::Lightning) {
        const auto target = trick->effectTarget ? findPlayer(*trick->effectTarget) : nullptr;
        if (target && pendingCard_) { const auto trickName = pendingCard_->name(); target->addJudgmentCard(pendingCard_); pendingCard_.reset(); log("[" + trickName + "] entered " + target->name() + "'s judgment zone."); }
    }
    else if (trick->cardType == CardType::IronChain && pendingCardUse_) {
        for (const auto targetId : pendingCardUse_->targets) {
            if (const auto target = findPlayer(targetId); target && target->isAlive()) { target->toggleChained(); log(target->name() + (target->isChained() ? " was chained by [Iron Chain]." : " was unchained by [Iron Chain].")); }
        }
        log("[Iron Chain] resolved.");
    }
    else if (trick->cardType == CardType::FireAttack && trick->effectTarget) {
        fireAttackContext_ = FireAttackContext {trick->source, *trick->effectTarget, {}};
        createFireAttackReveal();
        return;
    }
    finishPendingCardUse();
}

void GameEngine::createFireAttackReveal()
{
    if (!fireAttackContext_) return;
    const auto target = findPlayer(fireAttackContext_->target);
    if (!target || !target->isAlive() || target->handCards().empty()) { fireAttackContext_.reset(); finishPendingCardUse(); return; }
    CardSelectionRequest request {nextRequestId_++, CardSelectionPurpose::FireAttackReveal, target->id(), target->id(), 1, 1, true, false, {}};
    for (const auto& card : target->handCards()) request.selectableCards.push_back({card->id(), CardZone::Hand, std::nullopt, false, card->name()});
    pendingCardSelection_ = std::move(request);
    notifyInteractionCreated();
}

void GameEngine::createFireAttackDiscard()
{
    if (!fireAttackContext_ || !fireAttackContext_->revealedCardId) return;
    const auto source = findPlayer(fireAttackContext_->source);
    const auto target = findPlayer(fireAttackContext_->target);
    const auto revealed = target ? findHandCard(*target, *fireAttackContext_->revealedCardId) : nullptr;
    if (!source || !target || !revealed) { fireAttackContext_.reset(); finishPendingCardUse(); return; }
    CardSelectionRequest request {nextRequestId_++, CardSelectionPurpose::FireAttackDiscard, source->id(), source->id(), 0, 1, true, false, {}};
    for (const auto& card : source->handCards()) {
        if (card->suit() == revealed->suit()) request.selectableCards.push_back({card->id(), CardZone::Hand, std::nullopt, false, card->name()});
    }
    pendingCardSelection_ = std::move(request);
    notifyInteractionCreated();
}

void GameEngine::createCardSelectionRequest(CardSelectionPurpose purpose, PlayerId requester, PlayerId target)
{
    CardSelectionRequest request {nextRequestId_++, purpose, requester, target, 1, 1, true, true, {}};
    const auto targetPlayer = findPlayer(target);
    if (!targetPlayer) return;
    for (const auto &card : targetPlayer->handCards()) {
        request.selectableCards.push_back(SelectableCard {card->id(), CardZone::Hand, std::nullopt, true, {}});
    }
    for (const auto &[slot, card] : targetPlayer->equipmentCards()) {
        request.selectableCards.push_back(SelectableCard {card->id(), CardZone::Equipment, slot, false, card->name()});
    }
    pendingCardSelection_ = std::move(request);
    notifyInteractionCreated();
}

void GameEngine::resolvePendingSlash(bool dodged)
{
    if (!pendingCardUse_) return;
    const auto context = *pendingCardUse_;
    if (dodged) {
        log("[Slash] was dodged.");
        const auto source = findPlayer(context.source);
        const auto target = context.currentTargetIndex < context.targets.size() ? findPlayer(context.targets[context.currentTargetIndex]) : nullptr;
        if (source && target && source->isAlive() && target->isAlive()) {
            deferredSlashDamage_ = snapshotSlashDamage(context, target->id());
        } else {
            deferredSlashDamage_.reset();
        }
        if (!gameOver_ && source && target && source->isAlive() && target->isAlive() && weaponNamed(*source, "青龙偃月刀")) {
            bool hasSlash = false;
            for (const auto& card : source->handCards()) if (isSlashCard(card->type())) { hasSlash = true; break; }
            if (hasSlash) {
                deferredSlashDamage_.reset();
                qinglongContext_ = QinglongContext {source->id(), target->id()};
                pendingResponse_ = ResponseRequest {nextRequestId_++, ResponseType::QinglongSlash, source->id(), source->id(), context.cardId, true};
                log(source->name() + " may use [Slash] again with [Qinglong Blade].");
                notifyInteractionCreated();
                return;
            }
        }
        if (!gameOver_ && source && target && source->isAlive() && target->isAlive()
            && deferredSlashDamage_ && weaponNamed(*source, "贯石斧")) {
            createAxeDiscardRequest(*deferredSlashDamage_);
            return;
        }
        deferredSlashDamage_.reset();
        advanceSlashTarget();
        return;
    }
    const auto targetId = context.targets.at(context.currentTargetIndex); const auto target = findPlayer(targetId);
    if (!target) { advanceSlashTarget(); return; }
    const auto snapshot = snapshotSlashDamage(context, targetId);
    resolveSlashDamage(snapshot);
    if (!dyingContext_ && !pendingCardSelection_) advanceSlashTarget();
}

SlashDamageSnapshot GameEngine::snapshotSlashDamage(const CardUseContext& context, PlayerId targetId) const
{
    const auto source = findPlayer(context.source);
    const auto target = findPlayer(targetId);
    const bool gudingBladeBonus = source && target && weaponNamed(*source, "古锭刀") && target->handCards().empty();
    const bool blackNormalSlash = isNormalSlashForArmor(context) && pendingCard_
        && (pendingCard_->suit() == Suit::Spade || pendingCard_->suit() == Suit::Club);
    return SlashDamageSnapshot {context.source, targetId, context.cardType,
                                context.slashDamage + (gudingBladeBonus ? 1 : 0), context.damageNature,
                                context.ignoreArmor, blackNormalSlash, gudingBladeBonus};
}

void GameEngine::resolveSlashDamage(const SlashDamageSnapshot& snapshot)
{
    const auto target = findPlayer(snapshot.target);
    if (!target || !target->isAlive()) return;
    CardUseContext armorContext {snapshot.source, "slash-damage-snapshot", snapshot.cardType, {snapshot.target},
                                 snapshot.damageAmount, snapshot.damageNature, snapshot.ignoreArmor};
    if (snapshot.blackNormalSlash && armorApplies(*target, armorContext, "仁王盾")) {
        emitEquipmentEffect("仁王盾", EquipmentEffectType::DamagePrevented, target->id(), target->id(), 0, CardType::Slash);
        log(target->name() + " blocked a black [Slash] with [Renwang Shield].");
        return;
    }
    if (isNormalSlashForArmor(armorContext) && vineArmorApplies(*target, armorContext)) {
        emitEquipmentEffect("藤甲", EquipmentEffectType::DamagePrevented, target->id(), target->id(), 0, CardType::Slash);
        log(target->name() + " was protected by [Vine Armor].");
        return;
    }
    if (snapshot.gudingBladeBonus) {
        emitEquipmentEffect("古锭刀", EquipmentEffectType::BonusDamage, snapshot.source, target->id(), 1, CardType::Slash);
        if (const auto source = findPlayer(snapshot.source)) {
            log(source->name() + " increased Slash damage against empty-handed " + target->name() + " with [Guding Blade].");
        }
    }
    const auto source = findPlayer(snapshot.source);
    if (snapshot.ignoreArmor && source && weaponNamed(*source, "青釭剑")) emitEquipmentEffect("青釭剑", EquipmentEffectType::IgnoreArmor, source->id(), target->id());
    if (source && weaponNamed(*source, "寒冰剑") && snapshot.damageAmount > 0
        && (!target->handCards().empty() || !target->equipmentCards().empty())) {
        createIceSwordRequest(snapshot);
        return;
    }
    applyDamage(Damage {snapshot.source, snapshot.target, snapshot.damageAmount, snapshot.damageNature, snapshot.ignoreArmor, true});
}

void GameEngine::createIceSwordRequest(const SlashDamageSnapshot& snapshot)
{
    if (iceSwordContext_ || pendingCardSelection_) return;
    deferredSlashDamage_ = snapshot;
    const auto requestId = nextRequestId_++;
    iceSwordContext_ = IceSwordContext {snapshot.source, snapshot.target, requestId, snapshot, false, 0};
    pendingCardSelection_ = CardSelectionRequest {requestId, CardSelectionPurpose::IceSwordPrompt, snapshot.source, snapshot.target, 0, 1, false, false, {{"ice-sword-activate", CardZone::Hand, std::nullopt, false, "Activate Ice Sword"}}};
    log(findPlayer(snapshot.source)->name() + " may activate [Ice Sword] to prevent this Slash damage.");
    notifyInteractionCreated();
}

void GameEngine::finishIceSword(bool resumeDamage)
{
    const auto snapshot = deferredSlashDamage_;
    pendingCardSelection_.reset();
    iceSwordContext_.reset();
    deferredSlashDamage_.reset();
    if (resumeDamage && snapshot) applyDamage(Damage {snapshot->source, snapshot->target, snapshot->damageAmount, snapshot->damageNature, snapshot->ignoreArmor, true});
    if (!gameOver_ && !dyingContext_ && !chainDamageContext_ && pendingCardUse_) advanceSlashTarget();
}

void GameEngine::createIceSwordDiscardRequest()
{
    if (!iceSwordContext_) return;
    const auto target = findPlayer(iceSwordContext_->target);
    if (!target) { finishIceSword(false); return; }
    CardSelectionRequest request {nextRequestId_++, CardSelectionPurpose::IceSwordDiscard, iceSwordContext_->source, iceSwordContext_->target, 1, 1, true, true, {}};
    for (const auto& card : target->handCards()) request.selectableCards.push_back({card->id(), CardZone::Hand, std::nullopt, true, {}});
    for (const auto& [slot, card] : target->equipmentCards()) request.selectableCards.push_back({card->id(), CardZone::Equipment, slot, false, card->name()});
    if (request.selectableCards.empty()) { finishIceSword(false); return; }
    iceSwordContext_->requestId = request.requestId; pendingCardSelection_ = std::move(request);
    notifyInteractionCreated();
}

bool GameEngine::resumeDeferredSlashDamage()
{
    if (!deferredSlashDamage_ || gameOver_) return false;
    const auto snapshot = *deferredSlashDamage_;
    deferredSlashDamage_.reset();
    const auto source = findPlayer(snapshot.source);
    const auto target = findPlayer(snapshot.target);
    if (!source || !target || !source->isAlive() || !target->isAlive()) return false;
    resolveSlashDamage(snapshot);
    return true;
}

void GameEngine::createAxeDiscardRequest(const SlashDamageSnapshot& snapshot)
{
    const auto source = findPlayer(snapshot.source);
    if (!source || !source->isAlive() || !weaponNamed(*source, "贯石斧")) return;
    CardSelectionRequest request {nextRequestId_++, CardSelectionPurpose::AxeDiscard, source->id(), snapshot.target, 0, 2, true, true, {}};
    for (const auto& card : source->handCards()) request.selectableCards.push_back({card->id(), CardZone::Hand, std::nullopt, true, {}});
    for (const auto& [slot, card] : source->equipmentCards()) request.selectableCards.push_back({card->id(), CardZone::Equipment, slot, false, card->name()});
    axeContext_ = AxeContext {snapshot.source, snapshot.target, request.requestId};
    pendingCardSelection_ = std::move(request);
    log(source->name() + " may discard two cards with [Axe] to make the dodged [Slash] deal damage.");
    notifyInteractionCreated();
}

void GameEngine::finishAxeDiscard(bool resumeDamage)
{
    const auto context = axeContext_;
    pendingCardSelection_.reset();
    axeContext_.reset();
    if (resumeDamage) {
        log(findPlayer(context->source)->name() + " activated [Axe], discarded two cards, and made the original [Slash] deal damage to " + findPlayer(context->target)->name() + ".");
        resumeDeferredSlashDamage();
    } else {
        deferredSlashDamage_.reset();
        log("[Axe] was declined.");
    }
    if (!dyingContext_ && !pendingCardSelection_) advanceSlashTarget();
}

void GameEngine::cancelAxeContext(bool continueSlash)
{
    if (!axeContext_) return;
    axeContext_.reset();
    pendingCardSelection_.reset();
    deferredSlashDamage_.reset();
    if (continueSlash && !gameOver_ && !dyingContext_) advanceSlashTarget();
}

void GameEngine::advanceSlashTarget()
{
    if (!pendingCardUse_) return;
    ++pendingCardUse_->currentTargetIndex;
    while (pendingCardUse_->currentTargetIndex < pendingCardUse_->targets.size()) {
        const auto target = findPlayer(pendingCardUse_->targets[pendingCardUse_->currentTargetIndex]);
        if (target && target->isAlive()) { createDodgeRequest(*pendingCardUse_); return; }
        ++pendingCardUse_->currentTargetIndex;
    }
    if (borrowedSwordContext_) finishBorrowedSword(); else finishPendingCardUse();
}

void GameEngine::resolvePendingDuel(bool respondedWithSlash)
{
    if (!duelContext_) return;
    const auto context = *duelContext_;
    if (respondedWithSlash) {
        duelContext_->currentResponder = context.currentResponder == context.source ? context.target : context.source;
        createDuelRequest();
        return;
    }
    const PlayerId damageSource = context.currentResponder == context.source ? context.target : context.source;
    applyDamage(Damage {damageSource, context.currentResponder, 1});
    if (!dyingContext_) finishPendingCardUse();
}

void GameEngine::finishPendingCardUse()
{
    if (pendingCardUse_ && pendingCardUse_->cardType == CardType::Duel) log("[Duel] resolved.");
    if (pendingCard_) deck_.discard(pendingCard_);
    pendingCard_.reset();
    pendingCardUse_.reset();
    pendingCardSelection_.reset();
    doubleSwordContext_.reset();
    duelContext_.reset();
    multiTargetEffect_.reset();
    harvestContext_.reset();
    borrowedSwordContext_.reset();
    trickResolution_.reset();
    nullificationChain_.reset();
    fireAttackContext_.reset();
}

void GameEngine::applyDamage(const Damage &damage)
{
    auto target = findPlayer(damage.target); if (!target || !target->isAlive()) return;
    const auto armor = target->equipment(EquipmentSlot::Armor);
    const bool vine = !damage.ignoreArmor && armor && armor->name() == "藤甲";
    int amount = damage.amount + (vine && damage.nature == DamageNature::Fire ? 1 : 0);
    if (vine && damage.nature == DamageNature::Fire) {
        emitEquipmentEffect("藤甲", EquipmentEffectType::FireDamageIncreased, target->id(), target->id(), 1, CardType::FireSlash);
        log(target->name() + " increased Fire damage by 1 with [Vine Armor].");
    }
    if (!damage.ignoreArmor && armor && armor->name() == "白银狮子" && amount > 1) {
        amount = 1;
        emitEquipmentEffect("白银狮子", EquipmentEffectType::DamageCapped, target->id(), target->id(), 1);
        log(target->name() + " limited damage with [Silver Lion].");
    }
    const Damage adjusted {damage.source, damage.target, amount, damage.nature, damage.ignoreArmor};
    emitEvent(GameEventType::DamageCaused, adjusted.source, adjusted.target, std::to_string(adjusted.amount));
    emitEvent(GameEventType::DamageReceived, adjusted.source, adjusted.target, std::to_string(adjusted.amount));
    const int before = target->hp(); if (!damageSystem_.apply(adjusted, *target)) return;
    log(target->name() + " took " + std::to_string(adjusted.amount) + " damage. HP: " + std::to_string(before) + " -> " + std::to_string(target->hp()) + ".");
    emitEvent(GameEventType::HpChanged, damage.source, damage.target, std::to_string(target->hp()));
    if (target->hp() <= 0) enterDying(target->id(), damage.source);
    if (!chainDamageContext_ && target->isChained()
        && (damage.nature == DamageNature::Fire || damage.nature == DamageNature::Thunder)) {
        ChainDamageContext context {damage.source, damage.target, damage.amount, damage.nature, {damage.target}, {}, 0};
        const auto targetIt = std::find_if(players_.begin(), players_.end(), [&damage](const auto& player) { return player->id() == damage.target; });
        if (targetIt != players_.end()) {
            const auto start = static_cast<std::size_t>(std::distance(players_.begin(), targetIt));
            for (std::size_t offset = 1; offset < players_.size(); ++offset) {
                const auto& candidate = players_[(start + offset) % players_.size()];
                if (candidate->isAlive() && candidate->isChained()) context.propagationOrder.push_back(candidate->id());
            }
        }
        target->setChained(false);
        chainDamageContext_ = std::move(context);
        continueChainDamage();
    }
    if (damage.directSlash && !gameOver_ && target->isAlive() && target->hp() > 0 && !dyingContext_) createKirinBowRequest(damage);
}

void GameEngine::createKirinBowRequest(const Damage& damage)
{
    if (pendingCardSelection_ || kirinBowContext_) return;
    const auto source = findPlayer(damage.source); const auto target = findPlayer(damage.target);
    if (!source || !target || !weaponNamed(*source, "麒麟弓")) return;
    CardSelectionRequest request {nextRequestId_++, CardSelectionPurpose::KirinBowMount, source->id(), target->id(), 0, 1, false, true, {}};
    for (const auto slot : {EquipmentSlot::OffensiveHorse, EquipmentSlot::DefensiveHorse}) if (const auto mount = target->equipment(slot)) request.selectableCards.push_back({mount->id(), CardZone::Equipment, slot, false, mount->name()});
    if (request.selectableCards.empty()) return;
    kirinBowContext_ = KirinBowContext {source->id(), target->id(), request.requestId}; pendingCardSelection_ = std::move(request);
    notifyInteractionCreated();
}

void GameEngine::finishKirinBow()
{
    pendingCardSelection_.reset(); kirinBowContext_.reset();
    if (!dyingContext_ && !gameOver_) advanceSlashTarget();
}

std::shared_ptr<Card> GameEngine::removeEquipment(Player &player, EquipmentSlot slot, bool healSilverLion)
{
    auto removed = player.removeEquipment(slot);
    if (healSilverLion && removed && removed->name() == "白银狮子" && player.hp() < player.maxHp()) {
        emitEquipmentEffect("白银狮子", EquipmentEffectType::HealOnLeave, player.id(), player.id(), 1);
        applyRecover(Recover {player.id(), player.id(), 1});
        log(player.name() + " recovered 1 HP as [Silver Lion] left the equipment area.");
    }
    if (removed && !payingAxeCost_ && axeContext_ && axeContext_->source == player.id() && removed->name() == "贯石斧") {
        cancelAxeContext(true);
    }
    if (removed && kirinBowContext_ && kirinBowContext_->source == player.id() && removed->name() == "麒麟弓") {
        kirinBowContext_.reset(); pendingCardSelection_.reset();
    }
    if (removed && doubleSwordContext_ && doubleSwordContext_->source == player.id() && removed->name() == "雌雄双股剑") {
        cancelDoubleSwordContext(true);
    }
    if (removed && iceSwordContext_ && iceSwordContext_->source == player.id()
        && !iceSwordContext_->activated && removed->name() == "寒冰剑") {
        finishIceSword(true);
    }
    return removed;
}

void GameEngine::continueChainDamage()
{
    while (chainDamageContext_ && !dyingContext_ && !gameOver_
        && chainDamageContext_->currentIndex < chainDamageContext_->propagationOrder.size()) {
        const auto targetId = chainDamageContext_->propagationOrder[chainDamageContext_->currentIndex++];
        const auto target = findPlayer(targetId);
        if (!target || !target->isAlive() || !target->isChained()) continue;
        target->setChained(false);
        chainDamageContext_->processedPlayers.push_back(targetId);
        const auto source = findPlayer(chainDamageContext_->originalTarget);
        const char* nature = chainDamageContext_->nature == DamageNature::Fire ? "Fire" : "Thunder";
        log((source ? source->name() : std::string("A chained player")) + " propagated " + std::to_string(chainDamageContext_->amount)
            + " " + nature + " damage to " + target->name() + " through [Iron Chain].");
        applyDamage(Damage {chainDamageContext_->source, targetId, chainDamageContext_->amount, chainDamageContext_->nature});
    }
    if (chainDamageContext_ && (gameOver_ || (!dyingContext_ && chainDamageContext_->currentIndex >= chainDamageContext_->propagationOrder.size()))) chainDamageContext_.reset();
}

void GameEngine::applyRecover(const Recover &recover)
{
    auto target = findPlayer(recover.target);
    if (!target || target->status() == PlayerStatus::Dead || recover.amount <= 0) return;
    emitEvent(GameEventType::RecoverStarted, recover.source, recover.target, std::to_string(recover.amount));
    const int before = target->hp();
    target->recoverHp(recover.amount);
    const int recovered = target->hp() - before;
    if (recovered <= 0) return;
    log(target->name() + " recovered " + std::to_string(recovered) + " HP.");
    log(target->name() + " HP: " + std::to_string(before) + " -> " + std::to_string(target->hp()) + ".");
    emitEvent(GameEventType::HpRecovered, recover.source, recover.target, std::to_string(recovered));
    emitEvent(GameEventType::HpChanged, recover.source, recover.target, std::to_string(target->hp()));
}

void GameEngine::enterDying(PlayerId id, std::optional<PlayerId> damageSource)
{
    auto player = findPlayer(id); if (!player || !player->isAlive()) return;
    player->setStatus(PlayerStatus::Dying);
    DyingContext context;
    context.dyingPlayer = id;
    context.damageSource = damageSource;
    context.requiredRecovery = 1 - player->hp();
    for (std::size_t offset = 0; offset < players_.size(); ++offset) {
        const std::size_t index = (static_cast<std::size_t>(player->seat()) + offset) % players_.size();
        if (players_[index]->status() != PlayerStatus::Dead) context.rescueOrder.push_back(players_[index]->id());
    }
    dyingContext_ = std::move(context);
    log(player->name() + " entered Dying state.");
    log(player->name() + " requires " + std::to_string(dyingContext_->requiredRecovery) + " HP to be rescued.");
    emitEvent(GameEventType::PlayerDying, damageSource, id);
    requestNextRescue();
}

void GameEngine::requestNextRescue()
{
    if (!dyingContext_) return;
    if (dyingContext_->currentRescuerIndex >= dyingContext_->rescueOrder.size()) {
        const PlayerId dying = dyingContext_->dyingPlayer;
        const auto killer = dyingContext_->damageSource;
        log(findPlayer(dying)->name() + " could not leave Dying state.");
        dyingContext_.reset();
        killPlayer(dying, killer);
        return;
    }
    const PlayerId responder = dyingContext_->rescueOrder[dyingContext_->currentRescuerIndex];
    pendingResponse_ = ResponseRequest {nextRequestId_++, ResponseType::PeachRescue, dyingContext_->dyingPlayer, responder,
        pendingCardUse_ ? pendingCardUse_->cardId : "dying-rescue", true};
    log("Waiting for " + findPlayer(responder)->name() + " to use [Peach] to rescue " + findPlayer(dyingContext_->dyingPlayer)->name() + ".");
    notifyInteractionCreated();
}

void GameEngine::resolvePeachRescue(const ResponseRequest &request, const std::optional<CardId> &cardId)
{
    if (!dyingContext_) return;
    auto rescuer = findPlayer(request.responder);
    auto dying = findPlayer(dyingContext_->dyingPlayer);
    pendingResponse_.reset();
    if (!cardId) {
        log(rescuer->name() + " declined to use [Peach].");
        ++dyingContext_->currentRescuerIndex;
        requestNextRescue();
        return;
    }
    auto rescueCard = findHandCard(*rescuer, *cardId);
    const bool peach = rescueCard && rescueCard->type() == CardType::Peach;
    const bool selfRescueWine = rescueCard && rescueCard->type() == CardType::Wine
        && rescuer->id() == dying->id();
    if (!peach && !selfRescueWine) {
        log(rescuer->name() + " submitted an invalid rescue card.");
        requestNextRescue();
        return;
    }
    deck_.discard(rescuer->removeCard(rescueCard->id()));
    const char* rescueName = peach ? "Peach" : "Wine";
    log(rescuer->name() + " used [" + rescueName + "] to rescue " + dying->name() + ".");
    emitEvent(GameEventType::CardResponded, rescuer->id(), dying->id(), rescueName);
    applyRecover(Recover {rescuer->id(), dying->id(), 1});
    if (dying->hp() > 0) {
        dying->setStatus(PlayerStatus::Alive);
        log(dying->name() + " left Dying state.");
        dyingContext_.reset();
        if (chainDamageContext_) {
            continueChainDamage();
            if (chainDamageContext_ || dyingContext_ || gameOver_) return;
        }
        if (judgmentPhaseContext_) continueJudgmentPhase();
        else if (multiTargetEffect_) createMultiTargetResponse(); else if (borrowedSwordContext_) finishBorrowedSword(); else if (pendingCardUse_ && pendingCardUse_->targets.size() > 1) advanceSlashTarget(); else finishPendingCardUse();
        return;
    }
    dyingContext_->requiredRecovery = 1 - dying->hp();
    requestNextRescue();
}

void GameEngine::killPlayer(PlayerId id, std::optional<PlayerId> killerOverride)
{
    auto player = findPlayer(id); if (!player || player->status() == PlayerStatus::Dead) return;
    const bool deathDuringOwnJudgment = judgmentPhaseContext_ && currentPlayer() && currentPlayer()->id() == id;
    const auto killer = killerOverride ? killerOverride : (dyingContext_ ? dyingContext_->damageSource : std::optional<PlayerId> {});
    const auto victimIdentity = player->identity();
    if (iceSwordContext_ && (iceSwordContext_->source == id || iceSwordContext_->target == id)) {
        iceSwordContext_.reset();
        pendingCardSelection_.reset();
        deferredSlashDamage_.reset();
    }
    if (doubleSwordContext_ && (doubleSwordContext_->source == id || doubleSwordContext_->target == id)) {
        cancelDoubleSwordContext(false);
    }
    for (const auto& card : player->handCards()) deck_.discard(card);
    std::vector<CardId> handIds; for (const auto& card : player->handCards()) handIds.push_back(card->id());
    for (const auto& cardId : handIds) player->removeCard(cardId);
    for (const auto& [slot, card] : player->equipmentCards()) { deck_.discard(card); removeEquipment(*player, slot, false); }
    const bool resumeMultiTargetEffect = multiTargetEffect_.has_value();
    const bool resumeMultiTargetSlash = pendingCardUse_ && pendingCardUse_->targets.size() > 1;
    const bool resumeBorrowedSword = borrowedSwordContext_.has_value();
    const bool pendingCardSourceDied = pendingCardUse_ && pendingCardUse_->source == id;
    const bool cancelsQinglong = qinglongContext_ && (qinglongContext_->source == id || qinglongContext_->target == id);
    if (cancelsQinglong) qinglongContext_.reset();
    if (doubleSwordContext_ && (doubleSwordContext_->source == id || doubleSwordContext_->target == id)) doubleSwordContext_.reset();
    if (axeContext_ && (axeContext_->source == id || axeContext_->target == id)) cancelAxeContext(false);
    if (kirinBowContext_ && (kirinBowContext_->source == id || kirinBowContext_->target == id)) { kirinBowContext_.reset(); pendingCardSelection_.reset(); }
    if (iceSwordContext_ && (iceSwordContext_->source == id || iceSwordContext_->target == id)) { iceSwordContext_.reset(); pendingCardSelection_.reset(); }
    if (deferredSlashDamage_ && (deferredSlashDamage_->source == id || deferredSlashDamage_->target == id)) deferredSlashDamage_.reset();
    player->setStatus(PlayerStatus::Dead); pendingResponse_.reset(); pendingCardSelection_.reset(); dyingContext_.reset();
    if (pendingCardSourceDied) { pendingCardUse_.reset(); pendingCard_.reset(); }
    log(player->name() + " died." + (gameMode_ == GameMode::Identity ? " Identity: " + std::string(identityText(victimIdentity)) + "." : "")); emitEvent(GameEventType::PlayerDied, killer, id);
    if (!pendingCardSourceDied && !resumeMultiTargetEffect && !resumeMultiTargetSlash && !resumeBorrowedSword && !chainDamageContext_) finishPendingCardUse();
    if (gameMode_ == GameMode::Identity) applyIdentityDeathConsequences(killer, victimIdentity);
    checkGameOver();
    if (gameOver_) { chainDamageContext_.reset(); finishPendingCardUse(); return; }
    if (chainDamageContext_) {
        continueChainDamage();
        if (chainDamageContext_ || dyingContext_ || gameOver_) return;
    }
    if (deathDuringOwnJudgment) { judgmentPhaseContext_.reset(); completeTurn(); }
    else if (!pendingCardSourceDied && resumeMultiTargetEffect) createMultiTargetResponse();
    else if (!pendingCardSourceDied && resumeMultiTargetSlash) advanceSlashTarget();
    else if (!pendingCardSourceDied && resumeBorrowedSword) finishBorrowedSword();
    else if (currentPlayer() && currentPlayer()->id() == id) completeTurn();
}

void GameEngine::applyIdentityDeathConsequences(std::optional<PlayerId> killerId, PlayerIdentity victimIdentity)
{
    if (!killerId) return;
    const auto killer = findPlayer(*killerId);
    if (!killer || !killer->isAlive()) return;
    if (victimIdentity == PlayerIdentity::Rebel) {
        drawCards(killer->id(), 3);
        log(killer->name() + " drew 3 cards for defeating a Rebel.");
    } else if (killer->identity() == PlayerIdentity::Lord && victimIdentity == PlayerIdentity::Loyalist) {
        for (const auto& card : killer->handCards()) deck_.discard(card);
        std::vector<CardId> ids; for (const auto& card : killer->handCards()) ids.push_back(card->id());
        for (const auto& cardId : ids) killer->removeCard(cardId);
        for (const auto& [slot, card] : killer->equipmentCards()) { deck_.discard(card); removeEquipment(*killer, slot, false); }
        log(killer->name() + " discarded all cards for defeating a Loyalist.");
    }
}

void GameEngine::checkGameOver()
{
    if (gameOver_) return;
    std::vector<std::shared_ptr<Player>> alive;
    for (const auto& player : players_) if (player->isAlive()) alive.push_back(player);
    if (gameMode_ == GameMode::FreeForAll) {
        if (alive.size() != 1) return;
        winningSide_ = WinningSide::FreeForAll;
        winningPlayers_ = {alive.front()->id()};
        winner_ = alive.front()->id();
        gameOver_ = true;
        deferredSlashDamage_.reset(); axeContext_.reset(); kirinBowContext_.reset(); doubleSwordContext_.reset(); iceSwordContext_.reset(); pendingCardSelection_.reset();
        log(alive.front()->name() + " wins.");
        emitEvent(GameEventType::GameEnded, winner_);
        return;
    }
    const auto hasAlive = [&alive](PlayerIdentity identity) {
        return std::any_of(alive.begin(), alive.end(), [identity](const auto& player) { return player->identity() == identity; });
    };
    WinningSide side = WinningSide::None;
    if (hasAlive(PlayerIdentity::Lord) && !hasAlive(PlayerIdentity::Rebel) && !hasAlive(PlayerIdentity::Renegade)) side = WinningSide::LordSide;
    else if (!hasAlive(PlayerIdentity::Lord) && alive.size() == 1 && alive.front()->identity() == PlayerIdentity::Renegade) side = WinningSide::Renegade;
    else if (!hasAlive(PlayerIdentity::Lord)) side = WinningSide::RebelSide;
    if (side == WinningSide::None) return;
    winningSide_ = side;
    for (const auto& player : alive) {
        if ((side == WinningSide::LordSide && (player->identity() == PlayerIdentity::Lord || player->identity() == PlayerIdentity::Loyalist))
            || (side == WinningSide::RebelSide && player->identity() == PlayerIdentity::Rebel)
            || (side == WinningSide::Renegade && player->identity() == PlayerIdentity::Renegade)) winningPlayers_.push_back(player->id());
    }
    gameOver_ = true;
    deferredSlashDamage_.reset();
    axeContext_.reset();
    kirinBowContext_.reset();
    doubleSwordContext_.reset();
    iceSwordContext_.reset();
    pendingCardSelection_.reset();
    if (!winningPlayers_.empty()) winner_ = winningPlayers_.front();
    log((winner_ ? findPlayer(*winner_)->name() : std::string("No player")) + " wins.");
    emitEvent(GameEventType::GameEnded, winner_);
}

std::shared_ptr<Player> GameEngine::findPlayer(PlayerId id) const { for (const auto &p : players_) if (p->id() == id) return p; return nullptr; }
std::shared_ptr<Card> GameEngine::findHandCard(const Player &p, const CardId &id) const { for (const auto &c : p.handCards()) if (c && c->id() == id) return c; return nullptr; }
bool GameEngine::hasSelectableCards(const Player &player) const
{
    return !player.handCards().empty() || !player.equipmentCards().empty();
}
void GameEngine::appendPublicLog(std::string entry) { log(std::move(entry)); }
void GameEngine::log(std::string entry) { logEntries_.push_back(std::move(entry)); }
void GameEngine::emitEvent(GameEventType type, std::optional<PlayerId> source, std::optional<PlayerId> target, std::string detail, const Card* publicCard)
{
    GameEvent event {type, source, target, std::move(detail), nextGameEventId_++};
    if (publicCard) event.card = PublicEventCard {publicCard->name(), publicCard->type(), publicCard->suit(), publicCard->rank()};
    eventHistory_.push_back(event); dispatcher_.dispatch(event);
}
void GameEngine::notifyInteractionCreated()
{
    if (pendingResponse_) {
        emitEvent(GameEventType::InteractionRequested, pendingResponse_->requester, pendingResponse_->responder,
                  "response:" + std::to_string(pendingResponse_->requestId));
    } else if (pendingCardSelection_) {
        emitEvent(GameEventType::InteractionRequested, pendingCardSelection_->requester, pendingCardSelection_->target,
                  "selection:" + std::to_string(pendingCardSelection_->requestId));
    }
}
void GameEngine::emitEquipmentEffect(std::string equipmentName, EquipmentEffectType effect, PlayerId ownerId, PlayerId targetId, int value, CardType relatedCard)
{
    equipmentEffects_.push_back({nextEquipmentEffectId_++, std::move(equipmentName), effect, ownerId, targetId, value, relatedCard});
    if (equipmentEffects_.size() > 32) equipmentEffects_.erase(equipmentEffects_.begin());
}
bool GameEngine::actionBlocked() const noexcept
{
    return !started_ || gameOver_ || pendingResponse_.has_value() || pendingCardSelection_.has_value();
}

} // namespace sanguosha
