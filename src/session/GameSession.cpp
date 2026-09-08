#include "session/GameSession.h"

#include <algorithm>
#include <stdexcept>

namespace sanguosha {

GameSession::GameSession(const std::vector<std::string>& playerNames, bool startImmediately,
                         const std::vector<PlayerIdentity>& identities, const std::vector<PlayerControlType>& controlTypes, GameMode mode)
{
    engine_.createGame(playerNames, identities, controlTypes, mode);
    if (startImmediately) engine_.startGame();
}

void GameSession::startGame() { engine_.startGame(); }
void GameSession::configureGame(const std::vector<std::string>& playerNames,
                                const std::vector<PlayerIdentity>& identities,
                                const std::vector<PlayerControlType>& controlTypes, GameMode mode)
{
    engine_.createGame(playerNames, identities, controlTypes, mode);
    timeoutKind_ = TimeoutKind::None;
    timeoutPlayerId_ = 0;
    timeoutRemainingMs_ = 0;
    disconnectedPlayers_.clear();
}

SessionClientId GameSession::addClient(PlayerId playerId)
{
    if (!engine_.player(playerId)) throw std::invalid_argument("Client must bind to an existing player.");
    const auto id = nextClientId_++;
    clientPlayers_.emplace(id, playerId);
    return id;
}

void GameSession::disconnectClient(SessionClientId clientId)
{
    clientPlayers_.erase(clientId);
}

void GameSession::setPlayerConnected(PlayerId playerId, bool connected)
{
    if (connected) disconnectedPlayers_.erase(playerId);
    else disconnectedPlayers_.insert(playerId);
}

ActionResult GameSession::handlePlayerDisconnect(PlayerId playerId, bool resolveInteractions)
{
    if (!engine_.player(playerId)) return unauthorized();
    setPlayerConnected(playerId, false);
    if (!resolveInteractions) return {true, "Disconnected player retained during reconnect grace."};
    if (const auto& response = engine_.pendingResponse(); response && response->responder == playerId) return engine_.handleTimeout();
    if (const auto& selection = engine_.pendingCardSelection(); selection && selection->requester == playerId) return engine_.handleTimeout();
    if (const auto* current = engine_.currentPlayer(); current && current->id() == playerId
        && (engine_.currentPhase() == Phase::Play || engine_.currentPhase() == Phase::Discard)) return engine_.handleTimeout();
    return {true, "Disconnected player marked unavailable."};
}

ActionResult GameSession::submitAction(SessionClientId clientId, const GameAction& action)
{
    if (clientPlayers_.find(clientId) == clientPlayers_.end()) return recordActionResult(unauthorized());
    const PlayerId boundPlayer = playerFor(clientId);
    const bool authorized = std::visit([boundPlayer](const auto& submitted) { return submitted.playerId == boundPlayer; }, action);
    if (!authorized) return recordActionResult(unauthorized());
    auto result = std::visit([this](const auto& submitted) { return engine_.submitAction(submitted); }, action);
    if (!result.accepted) {
        if (result.message.find("game is over") != std::string::npos) result.error = ActionResult::Error::GameOver;
        else if (result.message.find("current player") != std::string::npos) result.error = ActionResult::Error::NotCurrentPlayer;
        else if (result.message.find("phase") != std::string::npos) result.error = ActionResult::Error::WrongPhase;
        else if (result.message.find("target") != std::string::npos) result.error = ActionResult::Error::InvalidTarget;
        else if (result.message.find("request ID") != std::string::npos) result.error = ActionResult::Error::RequestMismatch;
        else if (result.message.find("response") != std::string::npos) result.error = ActionResult::Error::InvalidResponse;
        else result.error = ActionResult::Error::InvalidCard;
    }
    return recordActionResult(result);
}

ActionResult GameSession::recordActionResult(ActionResult result)
{
#ifdef SANGUOSHA_TESTING
    if (!result.accepted) ++rejectedActionCount_;
#endif
    if (result.accepted) for (const auto& listener : actionListeners_) listener();
    return result;
}

ActionResult GameSession::submitCardSelection(SessionClientId clientId, std::uint64_t requestId, SelectionOptionId optionId)
{
    return submitCardSelection(clientId, requestId, std::vector<SelectionOptionId> {optionId});
}

ActionResult GameSession::submitCardSelection(SessionClientId clientId, std::uint64_t requestId, const std::vector<SelectionOptionId>& optionIds)
{
    if (clientPlayers_.find(clientId) == clientPlayers_.end()) return recordActionResult(unauthorized());
    const PlayerId requester = playerFor(clientId);
    const auto& request = engine_.pendingCardSelection();
    if (!request || request->requestId != requestId) return recordActionResult({false, "Card selection request ID does not match.", ActionResult::Error::RequestMismatch});
    if (request->requester != requester) return recordActionResult(unauthorized());
    std::vector<CardId> cards;
    for (const auto optionId : optionIds) {
        if (optionId == 0 || optionId > request->selectableCards.size()) return recordActionResult({false, "Selection option is invalid.", ActionResult::Error::InvalidCard});
        cards.push_back(request->selectableCards[optionId - 1].cardId);
    }
    const auto result = engine_.submitAction(SelectCardsAction {requester, requestId, std::move(cards)});
    return recordActionResult(result);
}

void GameSession::subscribeToActions(std::function<void()> listener)
{
    actionListeners_.push_back(std::move(listener));
}

PlayerViewState GameSession::viewFor(SessionClientId clientId) const { return buildViewFor(playerFor(clientId)); }

bool GameSession::canPlayCard(SessionClientId clientId, const CardId& cardId) const
{
    return engine_.canPlayCard(playerFor(clientId), cardId);
}

bool GameSession::canPlayCardOnTarget(SessionClientId clientId, const CardId& cardId, PlayerId targetId) const
{
    return engine_.canPlayCardOnTarget(playerFor(clientId), cardId, targetId);
}

bool GameSession::canTakeTurnAction(SessionClientId clientId) const
{
    return !engine_.gameOver() && !engine_.pendingResponse() && !engine_.pendingCardSelection()
        && engine_.currentPlayer() && engine_.currentPlayer()->id() == playerFor(clientId)
        && (engine_.currentPhase() == Phase::Play || engine_.currentPhase() == Phase::Discard);
}

std::vector<std::pair<PlayerId, PlayerId>> GameSession::borrowedSwordTargetPairs(SessionClientId clientId, const CardId& cardId) const
{
    return engine_.borrowedSwordTargetPairs(playerFor(clientId), cardId);
}

int GameSession::requiredDiscardCount(SessionClientId clientId) const
{
    const auto viewer = playerFor(clientId);
    return engine_.currentPlayer() && engine_.currentPlayer()->id() == viewer ? engine_.requiredDiscardCount() : 0;
}

std::vector<PlayerId> GameSession::aiPlayerIds() const
{
    std::vector<PlayerId> result;
    for (const auto& player : engine_.players()) if (player->controlType() == PlayerControlType::AI) result.push_back(player->id());
    return result;
}

void GameSession::restart(SessionClientId clientId)
{
    const auto playerId = playerFor(clientId);
    if (engine_.gameOver() && std::find(engine_.winningPlayers().begin(), engine_.winningPlayers().end(), playerId) != engine_.winningPlayers().end()) engine_.restart();
}
ActionResult GameSession::handleTimeout() { return engine_.handleTimeout(); }
void GameSession::appendPublicLog(std::string entry) { engine_.appendPublicLog(std::move(entry)); }
std::string GameSession::timeoutContextKey() const
{
    if (engine_.gameOver()) return {};
    if (const auto& response = engine_.pendingResponse()) return "response:" + std::to_string(response->requestId);
    if (const auto& selection = engine_.pendingCardSelection()) return "selection:" + std::to_string(selection->requestId);
    if (engine_.currentPhase() == Phase::Play || engine_.currentPhase() == Phase::Discard) {
        const auto* current = engine_.currentPlayer();
        return std::to_string(int(engine_.currentPhase())) + ":" + std::to_string(current ? current->id() : 0) + ":" + std::to_string(engine_.logEntries().size());
    }
    return {};
}
void GameSession::setTimeoutView(TimeoutKind kind, PlayerId playerId, int remainingMs)
{
    timeoutKind_ = kind; timeoutPlayerId_ = playerId; timeoutRemainingMs_ = remainingMs;
}
void GameSession::updateTimeoutView(int remainingMs)
{
    const auto* current = engine_.currentPlayer();
    if (const auto& response = engine_.pendingResponse()) { setTimeoutView(TimeoutKind::Response, response->responder, remainingMs); return; }
    if (const auto& selection = engine_.pendingCardSelection()) { setTimeoutView(TimeoutKind::Selection, selection->requester, remainingMs); return; }
    if (current && engine_.currentPhase() == Phase::Play) { setTimeoutView(TimeoutKind::Play, current->id(), remainingMs); return; }
    if (current && engine_.currentPhase() == Phase::Discard) { setTimeoutView(TimeoutKind::Discard, current->id(), remainingMs); return; }
    setTimeoutView(TimeoutKind::None, 0, 0);
}
void GameSession::subscribeToGameEvents(EventDispatcher::Listener listener) { engine_.subscribeEvents(std::move(listener)); }

#ifdef SANGUOSHA_TESTING
GameEngine& GameSession::testingEngine() noexcept { return engine_; }
#endif

PlayerId GameSession::playerFor(SessionClientId clientId) const
{
    const auto found = clientPlayers_.find(clientId);
    if (found == clientPlayers_.end()) throw std::invalid_argument("Unknown session client.");
    return found->second;
}

CardView GameSession::makeCardView(const Card& card)
{
    CardView result {card.id(), card.type(), card.suit(), card.rank(), card.name(), std::nullopt};
    if (card.equipmentData() && card.equipmentData()->attackRange > 0) result.attackRange = card.equipmentData()->attackRange;
    return result;
}

namespace {
EquipmentEffectTypeView equipmentEffectTypeView(EquipmentEffectType effect)
{
    switch (effect) {
    case EquipmentEffectType::IgnoreArmor: return EquipmentEffectTypeView::IgnoreArmor;
    case EquipmentEffectType::SlashConvertedToFire: return EquipmentEffectTypeView::SlashConvertedToFire;
    case EquipmentEffectType::BonusDamage: return EquipmentEffectTypeView::BonusDamage;
    case EquipmentEffectType::DamagePrevented: return EquipmentEffectTypeView::DamagePrevented;
    case EquipmentEffectType::FireDamageIncreased: return EquipmentEffectTypeView::FireDamageIncreased;
    case EquipmentEffectType::DamageCapped: return EquipmentEffectTypeView::DamageCapped;
    case EquipmentEffectType::HealOnLeave: return EquipmentEffectTypeView::HealOnLeave;
    case EquipmentEffectType::MultiTargetEnabled: return EquipmentEffectTypeView::MultiTargetEnabled;
    }
    return EquipmentEffectTypeView::IgnoreArmor;
}
}

PlayerViewState GameSession::buildViewFor(PlayerId viewer) const
{
    const auto* current = engine_.currentPlayer();
    PlayerViewState view {viewer, engine_.turnNumber(), current ? current->id() : 0, engine_.currentPhase()};
    view.gameMode = engine_.gameMode();
    view.publicEvents = engine_.eventHistory();
    if (engine_.gameMode() == GameMode::Identity) if (const auto* self = engine_.player(viewer)) view.selfIdentity = self->identity();
    const auto& effects = engine_.equipmentEffects();
    view.equipmentEffects.reserve(effects.size());
    for (const auto& event : effects) {
        view.equipmentEffects.push_back({event.eventId, event.equipmentName, equipmentEffectTypeView(event.effect),
                                         event.ownerId, event.targetId, event.value, event.relatedCard});
    }
    std::stable_sort(view.equipmentEffects.begin(), view.equipmentEffects.end(),
                     [](const auto& left, const auto& right) { return left.eventId < right.eventId; });
    for (const auto& player : engine_.players()) {
        PublicPlayerView publicView {player->id(), player->name(), player->hp(), player->maxHp(), player->isAlive(),
                                     player->status() == PlayerStatus::Dying, disconnectedPlayers_.find(player->id()) == disconnectedPlayers_.end(), player->isChained(), player->handCards().size(), {}};
        publicView.seat = player->seat();
        publicView.controlType = player->controlType();
        if (engine_.gameMode() == GameMode::Identity && (player->id() == viewer || player->identity() == PlayerIdentity::Lord || !player->isAlive())) publicView.identity = player->identity();
        for (const auto slot : {EquipmentSlot::Weapon, EquipmentSlot::Armor, EquipmentSlot::OffensiveHorse, EquipmentSlot::DefensiveHorse}) {
            if (const auto card = player->equipment(slot)) publicView.equipment.cardsBySlot[static_cast<std::size_t>(slot)] = makeCardView(*card);
        }
        for (const auto& card : player->judgmentCards()) if (card) publicView.judgmentCards.push_back(makeCardView(*card));
        view.players.push_back(std::move(publicView));
        if (player->id() == viewer) for (const auto& card : player->handCards()) {
            auto cardView = makeCardView(*card);
            cardView.minTargets = engine_.minTargetsForCard(viewer, card->id());
            cardView.maxTargets = engine_.maxTargetsForCard(viewer, card->id());
            view.ownHand.push_back(std::move(cardView));
        }
    }
    view.serpentSpearSlashTargets = engine_.virtualSlashTargets(viewer);
    if (const auto& request = engine_.pendingResponse()) {
        ResponseView response {request->type, request->requestId, request->requester, request->responder,
                               request->responder == viewer, request->allowDecline, {}};
        if (request->type == ResponseType::PeachRescue) response.targetId = request->requester;
        if (request->type == ResponseType::BorrowedSwordSlash) {
            if (const auto& context = engine_.borrowedSwordContext()) response.targetId = context->attackTarget;
        }
        if (request->type == ResponseType::QinglongSlash) {
            if (const auto& context = engine_.qinglongContext()) response.targetId = context->target;
        }
        if (const auto& aoe = engine_.multiTargetEffect()) {
            response.prompt = aoe->type == MultiTargetEffectType::BarbarianInvasion
                ? "南蛮入侵：请选择一张【杀】响应。"
                : "万箭齐发：请选择一张【闪】响应。";
        } else if (request->type == ResponseType::Slash && engine_.duelContext()) {
            response.prompt = "决斗：请选择一张【杀】响应。";
        } else if (request->type == ResponseType::BorrowedSwordSlash) {
            response.prompt = "借刀杀人：请选择一张【杀】响应，否则交出武器。";
        } else if (request->type == ResponseType::QinglongSlash) {
            const auto target = response.targetId ? engine_.player(response.targetId) : nullptr;
            response.prompt = "青龙偃月刀触发：" + (target ? target->name() : std::string("目标"))
                + " 使用了【闪】。请选择一张【杀】继续追击，或放弃。";
        } else if (request->type == ResponseType::Nullification) {
            response.prompt = "是否使用【无懈可击】？";
        } else if (request->type == ResponseType::PeachRescue) {
            const auto* dying = engine_.player(request->requester);
            response.prompt = "桃救援：请选择【桃】救援 " + (dying ? dying->name() : std::string("濒死角色")) + "。";
        } else if (request->type == ResponseType::Dodge) {
            response.prompt = "请选择一张【闪】响应。";
        } else {
            response.prompt = "请选择一张【杀】响应。";
        }
        if (response.isResponder) {
            const auto* responder = engine_.player(viewer);
            const auto required = request->type == ResponseType::Dodge ? CardType::Dodge
                : request->type == ResponseType::Nullification ? CardType::Nullification
                : (request->type == ResponseType::Slash || request->type == ResponseType::BorrowedSwordSlash || request->type == ResponseType::QinglongSlash) ? CardType::Slash : CardType::Peach;
            const bool slashResponse = request->type == ResponseType::Slash || request->type == ResponseType::BorrowedSwordSlash
                || request->type == ResponseType::QinglongSlash;
            for (const auto& card : responder->handCards()) {
                const bool validWineSelfRescue = request->type == ResponseType::PeachRescue
                    && viewer == request->requester && card->type() == CardType::Wine;
                if ((slashResponse ? isSlashCard(card->type()) : card->type() == required) || validWineSelfRescue) {
                    response.selectableCards.push_back(makeCardView(*card));
                }
            }
        }
        view.response = std::move(response);
    }
    if (const auto& request = engine_.pendingCardSelection(); request && request->requester == viewer) {
        CardSelectionView selection {request->requestId, request->purpose, request->target, request->minCount, request->maxCount, {}};
        SelectionOptionId optionId = 1;
        for (const auto& card : request->selectableCards) {
            std::optional<CardType> cardType;
            if (request->purpose == CardSelectionPurpose::Harvest) {
                if (const auto& harvest = engine_.harvestContext()) {
                    const auto found = std::find_if(harvest->pool.begin(), harvest->pool.end(), [&card](const auto& poolCard) { return poolCard && poolCard->id() == card.cardId; });
                    if (found != harvest->pool.end()) cardType = (*found)->type();
                }
            } else if (!card.hidden) {
                // A requester may plan costs using cards they already own.
                // Target hand options remain opaque and never enter this path.
                const auto* owner = engine_.player(viewer);
                if (owner && card.zone == CardZone::Hand) {
                    const auto found = std::find_if(owner->handCards().begin(), owner->handCards().end(),
                        [&card](const auto& owned) { return owned && owned->id() == card.cardId; });
                    if (found != owner->handCards().end()) cardType = (*found)->type();
                } else if (owner && card.zone == CardZone::Equipment && card.equipmentSlot) {
                    const auto equipped = owner->equipment(*card.equipmentSlot);
                    if (equipped && equipped->id() == card.cardId) cardType = equipped->type();
                }
            }
            selection.options.push_back({optionId++, card.zone, card.equipmentSlot, card.hidden, card.hidden ? "" : card.displayName, cardType});
        }
        if (request->purpose == CardSelectionPurpose::IceSwordPrompt) {
            selection.prompt = "寒冰剑：可发动以防止本次【杀】伤害，并弃置目标至多两张牌；也可直接跳过。";
        } else if (request->purpose == CardSelectionPurpose::IceSwordDiscard) {
            selection.prompt = "寒冰剑：请选择目标的一张手牌或装备弃置。";
        } else if (request->purpose == CardSelectionPurpose::DoubleSwordDiscard) {
            selection.prompt = "雌雄双股剑：可弃置一张手牌，否则攻击者摸一张牌。";
        }
        view.cardSelection = std::move(selection);
    }
    if (const auto& harvest = engine_.harvestContext()) {
        HarvestView publicHarvest; publicHarvest.currentPicker = harvest->currentTargetIndex < harvest->targetOrder.size() ? harvest->targetOrder[harvest->currentTargetIndex] : 0;
        for (const auto& card : harvest->pool) publicHarvest.pool.push_back(makeCardView(*card));
        for (const auto& choice : harvest->choices) {
            if (choice.card) publicHarvest.choices.push_back(HarvestChoiceView {choice.playerId, makeCardView(*choice.card)});
        }
        view.harvest = std::move(publicHarvest);
    }
    if (const auto& judgment = engine_.latestJudgment(); judgment && judgment->judgmentCard) {
        view.judgment = JudgmentView {judgment->player, judgment->delayedTrickId, judgment->delayedTrickType,
                                      makeCardView(*judgment->judgmentCard), judgment->succeeded};
    }
    if (const auto& fireAttack = engine_.fireAttackContext(); fireAttack && fireAttack->revealedCardId) {
        if (const auto* target = engine_.player(fireAttack->target)) {
            if (const auto revealed = std::find_if(target->handCards().begin(), target->handCards().end(), [&fireAttack](const auto& card) { return card && card->id() == *fireAttack->revealedCardId; }); revealed != target->handCards().end()) view.fireAttack = FireAttackView {fireAttack->target, makeCardView(**revealed)};
        }
    }
    if (const auto& response = engine_.pendingResponse(); response && response->type == ResponseType::Nullification) {
        if (const auto& trick = engine_.trickResolution()) {
            const int round = engine_.nullificationChain() ? engine_.nullificationChain()->nullificationCount + 1 : 1;
            view.nullification = NullificationContextView {trick->source, trick->cardType, trick->effectTarget, round};
        }
    }
    view.drawPileCount = engine_.deck().drawPileSize();
    view.discardPileCount = engine_.deck().discardPileSize();
    view.gameOver = engine_.gameOver();
    view.winner = engine_.winner();
    view.winningSide = engine_.winningSide();
    view.winningPlayers = engine_.winningPlayers();
    view.visibleLogs = engine_.logEntries();
    view.timeoutKind = timeoutKind_; view.timeoutPlayerId = timeoutPlayerId_; view.timeoutRemainingMs = timeoutRemainingMs_;
    return view;
}

ActionResult GameSession::unauthorized() const
{
    return {false, "Action player does not match the authenticated client.", ActionResult::Error::UnauthorizedPlayer};
}

} // namespace sanguosha
