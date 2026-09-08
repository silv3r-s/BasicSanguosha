#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <iostream>
#include <map>

#include "core/GameEngine.h"
#include "cards/StandardDeckDefinition.h"
#include "client/GameClientController.h"
#include "network/NetworkProtocol.h"
#include "HalberdTests.h"

using namespace sanguosha;

static std::shared_ptr<Card> card(const std::string &id, CardType type)
{
    return std::make_shared<Card>(id, id, type, Suit::Spade, 1);
}

static std::shared_ptr<Card> equipmentCard(const std::string &id, CardType type, int range = 0)
{
    const auto slot = equipmentSlotForCard(type);
    assert(slot.has_value());
    return std::make_shared<Card>(id, id, type, Suit::Spade, 1, EquipmentData {*slot, range});
}

static void clearHand(Player &player)
{
    std::vector<CardId> ids;
    for (const auto &existing : player.handCards()) ids.push_back(existing->id());
    for (const auto &id : ids) player.removeCard(id);
}

static GameEngine combatGame()
{
    GameEngine game;
    game.createGame({"Player 1", "Player 2"});
    game.startGame();
    clearHand(*game.players()[0]);
    clearHand(*game.players()[1]);
    assert(game.currentPlayer()->id() == 1 && game.currentPhase() == Phase::Play);
    return game;
}

static GameEngine fourPlayerGame()
{
    GameEngine game;
    game.createGame({"Player 1", "Player 2", "Player 3", "Player 4"});
    game.startGame();
    for (const auto &player : game.players()) clearHand(*player);
    assert(game.currentPlayer()->id() == 1 && game.currentPhase() == Phase::Play);
    return game;
}

static void finishCurrentTurn(GameEngine &game)
{
    assert(game.submitAction(EndPlayPhaseAction {game.currentPlayer()->id()}).accepted);
    if (game.currentPhase() == Phase::Discard) {
        std::vector<CardId> ids;
        for (int i = 0; i < game.requiredDiscardCount(); ++i) ids.push_back(game.currentPlayer()->handCards().at(i)->id());
        assert(game.submitAction(DiscardAction {game.currentPlayer()->id(), ids}).accepted);
    }
}

static void addAndDeclineSlash(GameEngine &game, const std::string &id)
{
    auto &source = *game.players()[0];
    auto &target = *game.players()[1];
    assert(game.currentPlayer()->id() == source.id() && game.currentPhase() == Phase::Play);
    clearHand(source); clearHand(target);
    source.addCard(card(id, CardType::Slash));
    assert(game.submitAction(PlayCardAction {source.id(), id, {target.id()}}).accepted);
    const auto request = *game.pendingResponse();
    assert(game.submitAction(RespondAction {target.id(), request.requestId, std::nullopt}).accepted);
}

static void advanceToPlayerOne(GameEngine &game)
{
    finishCurrentTurn(game);
    clearHand(*game.players()[1]);
    finishCurrentTurn(game);
    assert(game.currentPlayer()->id() == 1 && game.currentPhase() == Phase::Play);
}

static void declineAllRescues(GameEngine &game)
{
    while (game.pendingResponse()) {
        const auto request = *game.pendingResponse();
        assert(request.type == ResponseType::PeachRescue);
        assert(game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted);
    }
}

static void passNullificationChain(GameEngine &game)
{
    while (game.pendingResponse() && game.pendingResponse()->type == ResponseType::Nullification) {
        const auto request = *game.pendingResponse();
        assert(game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted);
    }
}

static void reducePlayerTwoToOneHp(GameEngine &game)
{
    for (int hit = 0; hit < 3; ++hit) {
        addAndDeclineSlash(game, "setup-hit-" + std::to_string(hit));
        advanceToPlayerOne(game);
    }
    assert(game.players()[1]->hp() == 1);
}

int main()
{
    { // Stage 2 regression.
        GameEngine game;
        game.createGame({"Player 1", "Player 2"});
        game.startGame();
        assert(game.players()[0]->handCards().size() == 6 && game.players()[1]->handCards().size() == 4);
        assert(!game.submitAction(EndPlayPhaseAction {2}).accepted);
        finishCurrentTurn(game);
        assert(game.currentPlayer()->id() == 2 && game.currentPhase() == Phase::Play);
        RandomGenerator random(123); Deck deck;
        deck.addToDrawPile(card("only", CardType::Slash));
        auto first = deck.drawCard(random); deck.discard(first);
        assert(deck.drawCard(random)->id() == "only");
        assert(!deck.drawCard(random));
    }
    { // Standard-deck definition: category/card counts, multiplicities, variants and horse metadata.
        const auto &definition = standardDeckDefinition();
        assert(definition.size() == 161);
        std::array<int, static_cast<std::size_t>(CardType::DefensiveHorse) + 1> counts {};
        std::map<std::string, int> keyCounts;
        for (const auto &entry : definition) {
            ++counts[static_cast<std::size_t>(entry.type)];
            keyCounts[std::to_string(static_cast<int>(entry.type)) + ":" + std::to_string(static_cast<int>(entry.suit)) + ":" + std::to_string(entry.rank) + ":" + entry.variant]++;
        }
        assert(counts[static_cast<std::size_t>(CardType::Slash)] == 30 && counts[static_cast<std::size_t>(CardType::FireSlash)] == 5 && counts[static_cast<std::size_t>(CardType::ThunderSlash)] == 9 && counts[static_cast<std::size_t>(CardType::Dodge)] == 24 && counts[static_cast<std::size_t>(CardType::Peach)] == 12 && counts[static_cast<std::size_t>(CardType::Wine)] == 5);
        assert(counts[static_cast<std::size_t>(CardType::Dismantlement)] == 6 && counts[static_cast<std::size_t>(CardType::Snatch)] == 5 && counts[static_cast<std::size_t>(CardType::Duel)] == 3 && counts[static_cast<std::size_t>(CardType::BorrowedSword)] == 2 && counts[static_cast<std::size_t>(CardType::ExNihilo)] == 4 && counts[static_cast<std::size_t>(CardType::Nullification)] == 7 && counts[static_cast<std::size_t>(CardType::IronChain)] == 6 && counts[static_cast<std::size_t>(CardType::FireAttack)] == 3 && counts[static_cast<std::size_t>(CardType::ArrowBarrage)] == 1 && counts[static_cast<std::size_t>(CardType::BarbarianInvasion)] == 3 && counts[static_cast<std::size_t>(CardType::PeachGarden)] == 1 && counts[static_cast<std::size_t>(CardType::Harvest)] == 2);
        assert(counts[static_cast<std::size_t>(CardType::Lightning)] == 2 && counts[static_cast<std::size_t>(CardType::Indulgence)] == 3 && counts[static_cast<std::size_t>(CardType::SupplyShortage)] == 2 && counts[static_cast<std::size_t>(CardType::Weapon)] == 13 && counts[static_cast<std::size_t>(CardType::Armor)] == 6 && counts[static_cast<std::size_t>(CardType::DefensiveHorse)] == 4 && counts[static_cast<std::size_t>(CardType::OffensiveHorse)] == 3);
        const auto key = [](CardType t, Suit s, int r, const char *v = "") { return std::to_string(static_cast<int>(t)) + ":" + std::to_string(static_cast<int>(s)) + ":" + std::to_string(r) + ":" + v; };
        assert(keyCounts[key(CardType::Slash, Suit::Spade, 8)] == 2 && keyCounts[key(CardType::Slash, Suit::Club, 10)] == 2 && keyCounts[key(CardType::Dodge, Suit::Diamond, 11)] == 3 && keyCounts[key(CardType::Peach, Suit::Heart, 6)] == 2);
        assert(keyCounts[key(CardType::Nullification, Suit::Diamond, 12, "EX-Q")] == 1 && keyCounts[key(CardType::Lightning, Suit::Heart, 12, "EX-Q")] == 1 && keyCounts[key(CardType::Weapon, Suit::Spade, 2, "EX-2")] == 1 && keyCounts[key(CardType::Armor, Suit::Club, 2, "EX-2")] == 1 && keyCounts[key(CardType::Weapon, Suit::Diamond, 12, "Q-SP")] == 1);
        std::map<std::string, CardType> horses; for (const auto &entry : definition) if (!entry.horseName.empty()) horses[entry.horseName] = entry.type;
        assert(horses["JueYing"] == CardType::DefensiveHorse && horses["ZhuaHuangFeiDian"] == CardType::DefensiveHorse && horses["HuaLiu"] == CardType::DefensiveHorse && horses["DiLu"] == CardType::DefensiveHorse && horses["DaYuan"] == CardType::OffensiveHorse && horses["ChiTu"] == CardType::OffensiveHorse && horses["ZiXing"] == CardType::OffensiveHorse);
    }
    { // Deck is generated from the authoritative definition; shuffle preserves its exact multiset.
        RandomGenerator random(456); Deck deck; deck.initialize(random);
        std::map<std::string, int> actual, expected;
        const auto cardKey = [](const Card &c) { return std::to_string(static_cast<int>(c.type())) + ":" + std::to_string(static_cast<int>(c.suit())) + ":" + std::to_string(c.rank()) + ":" + c.variant() + ":" + c.horseName(); };
        for (const auto &entry : standardDeckDefinition()) expected[std::to_string(static_cast<int>(entry.type)) + ":" + std::to_string(static_cast<int>(entry.suit)) + ":" + std::to_string(entry.rank) + ":" + entry.variant + ":" + entry.horseName]++;
        while (const auto drawn = deck.drawCard(random)) actual[cardKey(*drawn)]++;
        assert(actual == expected);
    }
    { // GameEngine start, shuffle, initial deal and first Draw retain delayed tricks in the authoritative draw pile.
        GameEngine game;
        game.testingSetRandomSeed(20260828);
        game.createGame({"Player 1", "Player 2"});
        game.startGame();
        assert(game.deck().drawPileSize() == 151);
        std::array<int, static_cast<std::size_t>(CardType::DefensiveHorse) + 1> remaining {};
        std::array<int, static_cast<std::size_t>(CardType::DefensiveHorse) + 1> dealt {};
        for (const auto& player : game.players()) for (const auto& dealtCard : player->handCards()) ++dealt[static_cast<std::size_t>(dealtCard->type())];
        while (const auto drawn = game.testingDrawFromDeck()) ++remaining[static_cast<std::size_t>(drawn->type())];
        const auto indulgence = static_cast<std::size_t>(CardType::Indulgence);
        const auto supply = static_cast<std::size_t>(CardType::SupplyShortage);
        const auto lightning = static_cast<std::size_t>(CardType::Lightning);
        assert(remaining[indulgence] > 0 && remaining[supply] > 0 && remaining[lightning] > 0);
        assert(remaining[indulgence] + dealt[indulgence] == 3);
        assert(remaining[supply] + dealt[supply] == 2);
        assert(remaining[lightning] + dealt[lightning] == 2);
        std::cout << "Authoritative post-deal draw pile: Indulgence=" << remaining[indulgence]
                  << ", SupplyShortage=" << remaining[supply] << ", Lightning=" << remaining[lightning] << ".\n";
    }
    { // Production-path regression: client action -> GameSession -> authoritative engine -> public equipment view -> Slash response.
        GameSession session({"Player 1", "Player 2"});
        GameClientController playerOne(session, 1); GameClientController playerTwo(session, 2);
        auto& engine = session.testingEngine(); auto& one = *engine.players()[0]; auto& two = *engine.players()[1];
        clearHand(one); clearHand(two);
        assert(playerOne.submit(EndPlayPhaseAction {1}).accepted);
        two.addCard(std::make_shared<Card>("production-bagua", "八卦阵", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0}));
        assert(playerTwo.submit(PlayCardAction {2, "production-bagua", {}}).accepted);
        playerOne.refresh();
        assert(playerOne.view().players[1].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Armor)]
            && playerOne.view().players[1].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Armor)]->displayName == "八卦阵");
        assert(playerTwo.submit(EndPlayPhaseAction {2}).accepted);
        engine.testingAddToDrawPile(std::make_shared<Card>("production-bagua-judge", "judge", CardType::Slash, Suit::Heart, 1));
        one.addCard(card("production-bagua-slash", CardType::Slash));
        assert(playerOne.submit(PlayCardAction {1, "production-bagua-slash", {2}}).accepted && !engine.pendingResponse() && two.hp() == 4);
    }
    { // A: Slash + Dodge.
        auto game = combatGame(); auto &a = *game.players()[0]; auto &b = *game.players()[1];
        a.addCard(card("slash-a", CardType::Slash)); b.addCard(card("dodge-a", CardType::Dodge));
        const auto discarded = game.deck().discardPileSize();
        assert(game.submitAction(PlayCardAction {a.id(), "slash-a", {b.id()}}).accepted);
        const auto request = *game.pendingResponse();
        assert(game.submitAction(RespondAction {b.id(), request.requestId, CardId("dodge-a")}).accepted);
        assert(b.hp() == 4 && !game.pendingResponse() && game.currentPhase() == Phase::Play);
        assert(game.deck().discardPileSize() == discarded + 2);
    }
    { // B, C, E and F.
        auto game = combatGame(); auto &a = *game.players()[0]; auto &b = *game.players()[1];
        a.addCard(card("dodge-c", CardType::Dodge));
        assert(!game.submitAction(PlayCardAction {a.id(), "dodge-c", {b.id()}}).accepted);
        a.addCard(card("slash-b", CardType::Slash));
        assert(game.submitAction(PlayCardAction {a.id(), "slash-b", {b.id()}}).accepted);
        const auto request = *game.pendingResponse();
        assert(!game.submitAction(RespondAction {a.id(), request.requestId, std::nullopt}).accepted);
        assert(!game.submitAction(RespondAction {b.id(), request.requestId + 1, std::nullopt}).accepted);
        assert(game.pendingResponse()->requestId == request.requestId);
        assert(game.submitAction(RespondAction {b.id(), request.requestId, std::nullopt}).accepted);
        assert(b.hp() == 3 && !game.pendingResponse());
    }
    { // D and I.
        auto game = combatGame(); auto &a = *game.players()[0]; auto &b = *game.players()[1];
        a.addCard(card("slash-d1", CardType::Slash)); a.addCard(card("slash-d2", CardType::Slash));
        assert(game.submitAction(PlayCardAction {a.id(), "slash-d1", {b.id()}}).accepted);
        auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {b.id(), request.requestId, std::nullopt}).accepted);
        assert(!game.submitAction(PlayCardAction {a.id(), "slash-d2", {b.id()}}).accepted);
        finishCurrentTurn(game); clearHand(*game.players()[1]); finishCurrentTurn(game);
        assert(game.currentPlayer()->id() == a.id() && game.slashUsedThisPhase() == 0);
        clearHand(a); clearHand(b); a.addCard(card("slash-d3", CardType::Slash));
        assert(game.submitAction(PlayCardAction {a.id(), "slash-d3", {b.id()}}).accepted);
    }
    { // G and H.
        auto game = combatGame();
        for (int hit = 1; hit <= 4; ++hit) {
            addAndDeclineSlash(game, "fatal-" + std::to_string(hit));
            if (hit < 4) advanceToPlayerOne(game);
        }
        declineAllRescues(game);
        assert(game.players()[1]->status() == PlayerStatus::Dead);
        assert(game.gameOver() && game.winner() && *game.winner() == 1);
        assert(!game.submitAction(EndPlayPhaseAction {1}).accepted);
    }
    { // Stage 4 A and B: Peach heals during Play and cannot be used at full HP.
        auto game = combatGame(); auto &a = *game.players()[0]; auto &b = *game.players()[1];
        advanceToPlayerOne(game); // P2's empty turn gives Player 1 a fresh Play phase.
        clearHand(a); clearHand(b);
        // Let Player 2 inflict one normal Slash so Player 1 is wounded.
        finishCurrentTurn(game); clearHand(b); b.addCard(card("hurt-a", CardType::Slash));
        assert(game.submitAction(PlayCardAction {b.id(), "hurt-a", {a.id()}}).accepted);
        auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {a.id(), request.requestId, std::nullopt}).accepted);
        assert(a.hp() == 3);
        finishCurrentTurn(game); assert(game.currentPlayer()->id() == a.id()); clearHand(a); a.addCard(card("peach-a", CardType::Peach));
        const auto discardBefore = game.deck().discardPileSize();
        assert(game.submitAction(PlayCardAction {a.id(), "peach-a", {}}).accepted);
        assert(a.hp() == 4 && game.deck().discardPileSize() == discardBefore + 1 && game.currentPhase() == Phase::Play);
        a.addCard(card("peach-full", CardType::Peach));
        assert(!game.submitAction(PlayCardAction {a.id(), "peach-full", {}}).accepted);
    }
    { // Stage 4 C and D: self rescue and another player's rescue retain Player 1's turn.
        auto selfGame = combatGame(); reducePlayerTwoToOneHp(selfGame);
        auto &a = *selfGame.players()[0]; auto &b = *selfGame.players()[1];
        clearHand(a); clearHand(b); a.addCard(card("fatal-self", CardType::Slash)); b.addCard(card("peach-self", CardType::Peach));
        assert(selfGame.submitAction(PlayCardAction {a.id(), "fatal-self", {b.id()}}).accepted);
        auto dodge = *selfGame.pendingResponse(); assert(selfGame.submitAction(RespondAction {b.id(), dodge.requestId, std::nullopt}).accepted);
        auto rescue = *selfGame.pendingResponse(); assert(rescue.type == ResponseType::PeachRescue && rescue.responder == b.id());
        // Pending dying rescue blocks ordinary actions until the response resolves.
        assert(!selfGame.submitAction(EndPlayPhaseAction {a.id()}).accepted);
        assert(selfGame.submitAction(RespondAction {b.id(), rescue.requestId, CardId("peach-self")}).accepted);
        assert(b.hp() == 1 && b.status() == PlayerStatus::Alive && !selfGame.gameOver() && selfGame.currentPlayer()->id() == a.id() && selfGame.currentPhase() == Phase::Play);

        auto otherGame = combatGame(); reducePlayerTwoToOneHp(otherGame);
        auto &otherA = *otherGame.players()[0]; auto &otherB = *otherGame.players()[1];
        clearHand(otherA); clearHand(otherB); otherA.addCard(card("fatal-other", CardType::Slash)); otherA.addCard(card("peach-other", CardType::Peach));
        assert(otherGame.submitAction(PlayCardAction {otherA.id(), "fatal-other", {otherB.id()}}).accepted);
        dodge = *otherGame.pendingResponse(); assert(otherGame.submitAction(RespondAction {otherB.id(), dodge.requestId, std::nullopt}).accepted);
        rescue = *otherGame.pendingResponse(); assert(otherGame.submitAction(RespondAction {otherB.id(), rescue.requestId, std::nullopt}).accepted);
        rescue = *otherGame.pendingResponse(); assert(rescue.responder == otherA.id());
        assert(otherGame.submitAction(RespondAction {otherA.id(), rescue.requestId, CardId("peach-other")}).accepted);
        assert(otherB.status() == PlayerStatus::Alive && otherB.hp() == 1 && otherGame.currentPlayer()->id() == otherA.id());
    }
    { // Stage 4 F, G and H: two Peaches rescue -1 HP; Wine buffs one Slash and is consumed on Dodge.
        auto game = combatGame(); reducePlayerTwoToOneHp(game);
        auto &a = *game.players()[0]; auto &b = *game.players()[1];
        clearHand(a); clearHand(b); a.addCard(card("wine-f", CardType::Wine)); a.addCard(card("slash-f", CardType::Slash)); b.addCard(card("peach-f1", CardType::Peach)); b.addCard(card("peach-f2", CardType::Peach));
        assert(game.submitAction(PlayCardAction {a.id(), "wine-f", {}}).accepted && a.hasWineBuff());
        assert(game.submitAction(PlayCardAction {a.id(), "slash-f", {b.id()}}).accepted && !a.hasWineBuff());
        auto dodge = *game.pendingResponse(); assert(game.submitAction(RespondAction {b.id(), dodge.requestId, std::nullopt}).accepted);
        assert(b.hp() == -1);
        auto rescue = *game.pendingResponse(); assert(game.submitAction(RespondAction {b.id(), rescue.requestId, CardId("peach-f1")}).accepted);
        assert(b.hp() == 0 && b.status() == PlayerStatus::Dying);
        rescue = *game.pendingResponse(); assert(game.submitAction(RespondAction {b.id(), rescue.requestId, CardId("peach-f2")}).accepted);
        assert(b.hp() == 1 && b.status() == PlayerStatus::Alive);

        auto dodgeGame = combatGame(); auto &dodgeA = *dodgeGame.players()[0]; auto &dodgeB = *dodgeGame.players()[1];
        dodgeA.addCard(card("wine-h", CardType::Wine)); dodgeA.addCard(card("slash-h", CardType::Slash)); dodgeB.addCard(card("dodge-h", CardType::Dodge));
        assert(dodgeGame.submitAction(PlayCardAction {dodgeA.id(), "wine-h", {}}).accepted);
        assert(dodgeGame.submitAction(PlayCardAction {dodgeA.id(), "slash-h", {dodgeB.id()}}).accepted && !dodgeA.hasWineBuff());
        dodge = *dodgeGame.pendingResponse(); assert(dodgeGame.submitAction(RespondAction {dodgeB.id(), dodge.requestId, CardId("dodge-h")}).accepted);
        assert(dodgeB.hp() == 4 && !dodgeA.hasWineBuff());
    }
    { // Stage 4 I and J: Wine expires at turn end and cannot stack.
        auto game = combatGame(); auto &a = *game.players()[0];
        a.addCard(card("wine-i1", CardType::Wine)); a.addCard(card("wine-i2", CardType::Wine));
        assert(game.submitAction(PlayCardAction {a.id(), "wine-i1", {}}).accepted && a.hasWineBuff());
        assert(!game.submitAction(PlayCardAction {a.id(), "wine-i2", {}}).accepted);
        finishCurrentTurn(game);
        assert(!a.hasWineBuff());
    }
    { // Stage 5 A: Ex Nihilo is a net +1 hand card and remains in Play.
        auto game = combatGame(); auto &a = *game.players()[0];
        a.addCard(card("ex-nihilo", CardType::ExNihilo)); a.addCard(card("extra-1", CardType::Slash)); a.addCard(card("extra-2", CardType::Dodge));
        const auto discarded = game.deck().discardPileSize();
        assert(a.handCards().size() == 3);
        assert(game.submitAction(PlayCardAction {a.id(), "ex-nihilo", {}}).accepted);
        while (game.pendingResponse()) { const auto request = *game.pendingResponse(); assert(request.type == ResponseType::Nullification); assert(game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        assert(a.handCards().size() == 4 && game.deck().discardPileSize() == discarded + 1 && game.currentPhase() == Phase::Play);
    }
    { // Stage 5 B, L, M and N: Dismantlement selects one target hand card through a locked request.
        auto game = combatGame(); auto &a = *game.players()[0]; auto &b = *game.players()[1];
        a.addCard(card("dismantlement", CardType::Dismantlement)); a.addCard(card("blocked-play", CardType::Wine));
        b.addCard(card("target-1", CardType::Slash)); b.addCard(card("target-2", CardType::Dodge)); b.addCard(card("target-3", CardType::Peach));
        const auto discarded = game.deck().discardPileSize();
        assert(game.canPlayCardOnTarget(a.id(), "dismantlement", b.id()));
        assert(game.submitAction(PlayCardAction {a.id(), "dismantlement", {b.id()}}).accepted);
        passNullificationChain(game);
        const auto request = *game.pendingCardSelection();
        assert(request.purpose == CardSelectionPurpose::Dismantlement && request.requester == a.id() && request.target == b.id()
            && request.requestId > 0 && request.minCount == 1 && request.maxCount == 1 && request.selectableCards.size() == 3
            && std::all_of(request.selectableCards.begin(), request.selectableCards.end(), [](const auto& card) { return card.zone == CardZone::Hand && card.hidden; }));
        assert(!game.submitAction(SelectCardsAction {a.id(), request.requestId + 1, {"target-1"}}).accepted);
        assert(!game.submitAction(SelectCardsAction {a.id(), request.requestId, {"blocked-play"}}).accepted);
        assert(!game.submitAction(EndPlayPhaseAction {a.id()}).accepted);
        assert(!game.submitAction(PlayCardAction {a.id(), "blocked-play", {}}).accepted);
        assert(!game.submitAction(DiscardAction {a.id(), {}}).accepted);
        assert(!game.submitAction(RespondAction {a.id(), request.requestId, std::nullopt}).accepted);
        assert(game.submitAction(SelectCardsAction {a.id(), request.requestId, {"target-1"}}).accepted);
        assert(b.handCards().size() == 2 && !game.pendingCardSelection() && game.deck().discardPileSize() == discarded + 2);
    }
    { // Stage 5 C: an empty target cannot receive Dismantlement.
        auto game = combatGame(); auto &a = *game.players()[0]; auto &b = *game.players()[1];
        a.addCard(card("dismantlement-empty", CardType::Dismantlement));
        assert(!game.canPlayCardOnTarget(a.id(), "dismantlement-empty", b.id()));
        assert(!game.submitAction(PlayCardAction {a.id(), "dismantlement-empty", {b.id()}}).accepted);
    }
    { // Stage 5 D: Snatch obtains the chosen target hand card.
        auto game = combatGame(); auto &a = *game.players()[0]; auto &b = *game.players()[1];
        a.addCard(card("snatch", CardType::Snatch)); b.addCard(card("snatched", CardType::Peach));
        const auto discarded = game.deck().discardPileSize();
        assert(game.submitAction(PlayCardAction {a.id(), "snatch", {b.id()}}).accepted);
        passNullificationChain(game);
        const auto request = *game.pendingCardSelection();
        assert(game.submitAction(SelectCardsAction {a.id(), request.requestId, {"snatched"}}).accepted);
        assert(b.handCards().empty() && a.handCards().size() == 1 && a.handCards().front()->id() == "snatched" && game.deck().discardPileSize() == discarded + 1);
    }
    { // Stage 5 E: Snatch respects circular multiplayer distance.
        GameEngine game; game.createGame({"Player 1", "Player 2", "Player 3", "Player 4"}); game.startGame();
        for (const auto &player : game.players()) clearHand(*player);
        auto &a = *game.players()[0]; auto &far = *game.players()[2];
        a.addCard(card("far-snatch", CardType::Snatch)); far.addCard(card("far-card", CardType::Slash));
        assert(!game.canPlayCardOnTarget(a.id(), "far-snatch", far.id()));
    }
    { // Stage 5 F through I: Duel alternates Slash responses and rejects wrong input.
        auto game = combatGame(); auto &a = *game.players()[0]; auto &b = *game.players()[1];
        a.addCard(card("duel-f", CardType::Duel));
        assert(game.submitAction(PlayCardAction {a.id(), "duel-f", {b.id()}}).accepted);
        passNullificationChain(game);
        auto request = *game.pendingResponse();
        assert(request.type == ResponseType::Slash && request.responder == b.id());
        assert(!game.submitAction(RespondAction {a.id(), request.requestId, std::nullopt}).accepted);
        b.addCard(card("wrong-dodge", CardType::Dodge)); b.addCard(card("wrong-peach", CardType::Peach));
        assert(!game.submitAction(RespondAction {b.id(), request.requestId, CardId("wrong-dodge")}).accepted);
        assert(!game.submitAction(RespondAction {b.id(), request.requestId, CardId("wrong-peach")}).accepted);
        assert(game.submitAction(RespondAction {b.id(), request.requestId, std::nullopt}).accepted);
        assert(b.hp() == 3 && !game.pendingResponse() && !game.duelContext());

        auto alternating = combatGame(); auto &altA = *alternating.players()[0]; auto &altB = *alternating.players()[1];
        altA.addCard(card("duel-g", CardType::Duel)); altA.addCard(card("duel-g-a", CardType::Slash)); altB.addCard(card("duel-g-b", CardType::Slash));
        const auto discarded = alternating.deck().discardPileSize();
        assert(alternating.submitAction(PlayCardAction {altA.id(), "duel-g", {altB.id()}}).accepted);
        passNullificationChain(alternating);
        request = *alternating.pendingResponse(); assert(alternating.submitAction(RespondAction {altB.id(), request.requestId, CardId("duel-g-b")}).accepted);
        request = *alternating.pendingResponse(); assert(request.responder == altA.id() && alternating.submitAction(RespondAction {altA.id(), request.requestId, CardId("duel-g-a")}).accepted);
        request = *alternating.pendingResponse(); assert(request.responder == altB.id() && alternating.submitAction(RespondAction {altB.id(), request.requestId, std::nullopt}).accepted);
        assert(altB.hp() == 3 && alternating.deck().discardPileSize() == discarded + 3);

        // Elemental Slashes satisfy the Duel response requirement, but do not deal
        // elemental damage merely by being used as responses.
        auto elemental = combatGame(); auto &elementalA = *elemental.players()[0]; auto &elementalB = *elemental.players()[1];
        elementalA.addCard(card("duel-elemental", CardType::Duel)); elementalA.addCard(card("duel-thunder", CardType::ThunderSlash));
        elementalB.addCard(card("duel-fire", CardType::FireSlash));
        assert(elemental.submitAction(PlayCardAction {elementalA.id(), "duel-elemental", {elementalB.id()}}).accepted);
        passNullificationChain(elemental);
        request = *elemental.pendingResponse(); assert(request.responder == elementalB.id() && elemental.submitAction(RespondAction {elementalB.id(), request.requestId, CardId("duel-fire")}).accepted);
        request = *elemental.pendingResponse(); assert(request.responder == elementalA.id() && elemental.submitAction(RespondAction {elementalA.id(), request.requestId, CardId("duel-thunder")}).accepted);
        request = *elemental.pendingResponse(); assert(request.responder == elementalB.id() && elemental.submitAction(RespondAction {elementalB.id(), request.requestId, std::nullopt}).accepted);
        assert(elementalA.hp() == 4 && elementalB.hp() == 3 && !elemental.duelContext());
    }
    { // Stage 5 J and K: Duel damage reuses death and Peach rescue flows.
        auto deathGame = combatGame(); reducePlayerTwoToOneHp(deathGame);
        auto &deathA = *deathGame.players()[0]; auto &deathB = *deathGame.players()[1];
        clearHand(deathA); clearHand(deathB); deathA.addCard(card("duel-death", CardType::Duel));
        assert(deathGame.submitAction(PlayCardAction {deathA.id(), "duel-death", {deathB.id()}}).accepted);
        passNullificationChain(deathGame);
        auto request = *deathGame.pendingResponse(); assert(deathGame.submitAction(RespondAction {deathB.id(), request.requestId, std::nullopt}).accepted);
        declineAllRescues(deathGame);
        assert(deathB.status() == PlayerStatus::Dead && deathGame.gameOver() && deathGame.winner() && *deathGame.winner() == deathA.id());

        auto rescueGame = combatGame(); reducePlayerTwoToOneHp(rescueGame);
        auto &rescueA = *rescueGame.players()[0]; auto &rescueB = *rescueGame.players()[1];
        clearHand(rescueA); clearHand(rescueB); rescueA.addCard(card("duel-rescue", CardType::Duel)); rescueB.addCard(card("duel-peach", CardType::Peach));
        assert(rescueGame.submitAction(PlayCardAction {rescueA.id(), "duel-rescue", {rescueB.id()}}).accepted);
        passNullificationChain(rescueGame);
        request = *rescueGame.pendingResponse(); assert(rescueGame.submitAction(RespondAction {rescueB.id(), request.requestId, std::nullopt}).accepted);
        request = *rescueGame.pendingResponse(); assert(request.type == ResponseType::PeachRescue && rescueGame.submitAction(RespondAction {rescueB.id(), request.requestId, CardId("duel-peach")}).accepted);
        assert(rescueB.status() == PlayerStatus::Alive && rescueB.hp() == 1 && !rescueGame.duelContext() && rescueGame.currentPlayer()->id() == rescueA.id() && rescueGame.currentPhase() == Phase::Play);
    }
    { // Stage 6 A, B and C: equipment occupies explicit slots and replaces the old card.
        auto game = combatGame(); auto &a = *game.players()[0];
        a.addCard(equipmentCard("qinggang", CardType::Weapon, 2));
        a.addCard(equipmentCard("fangtian", CardType::Weapon, 4));
        a.addCard(equipmentCard("bagua", CardType::Armor));
        a.addCard(equipmentCard("renwang", CardType::Armor));
        const auto discarded = game.deck().discardPileSize();
        assert(game.attackRange(a.id()) == 1);
        assert(game.submitAction(PlayCardAction {a.id(), "qinggang", {}}).accepted);
        assert(a.equipment(EquipmentSlot::Weapon)->id() == "qinggang" && game.attackRange(a.id()) == 2);
        assert(game.submitAction(PlayCardAction {a.id(), "fangtian", {}}).accepted);
        assert(a.equipment(EquipmentSlot::Weapon)->id() == "fangtian" && game.attackRange(a.id()) == 4 && game.deck().discardPileSize() == discarded + 1);
        assert(game.submitAction(PlayCardAction {a.id(), "bagua", {}}).accepted);
        assert(game.submitAction(PlayCardAction {a.id(), "renwang", {}}).accepted);
        assert(a.equipment(EquipmentSlot::Armor)->id() == "renwang" && game.deck().discardPileSize() == discarded + 2);
    }
    { // Stage 6 D through G: circular distance applies both horse modifiers and stays at least one.
        auto game = fourPlayerGame(); auto &a = *game.players()[0]; auto &b = *game.players()[1]; auto &c = *game.players()[2];
        a.addCard(equipmentCard("offensive", CardType::OffensiveHorse));
        assert(game.distanceBetween(a.id(), c.id()) == 2);
        assert(game.submitAction(PlayCardAction {a.id(), "offensive", {}}).accepted);
        assert(game.distanceBetween(a.id(), c.id()) == 1 && game.distanceBetween(a.id(), b.id()) == 1);
        finishCurrentTurn(game); clearHand(b); b.addCard(equipmentCard("defensive", CardType::DefensiveHorse));
        assert(game.submitAction(PlayCardAction {b.id(), "defensive", {}}).accepted);
        assert(game.distanceBetween(a.id(), b.id()) == 1);
        assert(game.distanceBetween(a.id(), c.id()) == 1);
        finishCurrentTurn(game); clearHand(*game.players()[2]); finishCurrentTurn(game); clearHand(*game.players()[3]); finishCurrentTurn(game);
        assert(game.currentPlayer()->id() == a.id());
        assert(game.distanceBetween(a.id(), b.id()) == 1);
    }
    { // Stage 6 H through K: equipment modifies Slash and Snatch legality through the same distance API.
        auto game = fourPlayerGame(); auto &a = *game.players()[0]; auto &b = *game.players()[1]; auto &c = *game.players()[2];
        a.addCard(card("far-slash", CardType::Slash)); a.addCard(equipmentCard("range-two", CardType::Weapon, 2));
        assert(!game.canPlayCardOnTarget(a.id(), "far-slash", c.id()));
        assert(game.submitAction(PlayCardAction {a.id(), "range-two", {}}).accepted);
        assert(game.canPlayCardOnTarget(a.id(), "far-slash", c.id()));

        auto horseGame = fourPlayerGame(); auto &horseA = *horseGame.players()[0]; auto &horseB = *horseGame.players()[1];
        horseA.addCard(card("near-slash", CardType::Slash)); horseA.addCard(card("near-snatch", CardType::Snatch)); horseA.addCard(equipmentCard("offensive-k", CardType::OffensiveHorse));
        finishCurrentTurn(horseGame); clearHand(horseB); horseB.addCard(equipmentCard("defensive-i", CardType::DefensiveHorse));
        assert(horseGame.submitAction(PlayCardAction {horseB.id(), "defensive-i", {}}).accepted);
        finishCurrentTurn(horseGame); clearHand(*horseGame.players()[2]); finishCurrentTurn(horseGame); clearHand(*horseGame.players()[3]); finishCurrentTurn(horseGame);
        assert(!horseGame.canPlayCardOnTarget(horseA.id(), "near-slash", horseB.id()));
        assert(!horseGame.canPlayCardOnTarget(horseA.id(), "near-snatch", horseB.id()));
        assert(horseGame.submitAction(PlayCardAction {horseA.id(), "offensive-k", {}}).accepted);
        assert(horseGame.canPlayCardOnTarget(horseA.id(), "near-slash", horseB.id()));
        assert(horseGame.canPlayCardOnTarget(horseA.id(), "near-snatch", horseB.id()));
    }
    { // Stage 6 L through O: Dismantlement and Snatch select equipment as well as hidden hand cards.
        auto setupEquipmentTarget = [](GameEngine &game, const std::string &weaponId) {
            auto &b = *game.players()[1];
            finishCurrentTurn(game); clearHand(b); b.addCard(equipmentCard(weaponId, CardType::Weapon, 4));
            assert(game.submitAction(PlayCardAction {b.id(), weaponId, {}}).accepted);
            finishCurrentTurn(game);
            assert(game.currentPlayer()->id() == 1 && game.currentPhase() == Phase::Play);
        };
        auto dismantleGame = combatGame(); auto &dismantleA = *dismantleGame.players()[0]; auto &dismantleB = *dismantleGame.players()[1];
        setupEquipmentTarget(dismantleGame, "fangtian-m"); dismantleA.addCard(card("dismantle-weapon", CardType::Dismantlement));
        const auto discarded = dismantleGame.deck().discardPileSize();
        assert(dismantleB.handCards().empty());
        assert(dismantleGame.canPlayCardOnTarget(dismantleA.id(), "dismantle-weapon", dismantleB.id()));
        assert(dismantleGame.submitAction(PlayCardAction {dismantleA.id(), "dismantle-weapon", {dismantleB.id()}}).accepted);
        passNullificationChain(dismantleGame);
        const auto dismantleRequest = *dismantleGame.pendingCardSelection();
        assert(dismantleRequest.selectableCards.size() == 1 && dismantleRequest.selectableCards.front().zone == CardZone::Equipment);
        assert(dismantleGame.submitAction(SelectCardsAction {dismantleA.id(), dismantleRequest.requestId, {"fangtian-m"}}).accepted);
        assert(!dismantleB.hasEquipment(EquipmentSlot::Weapon) && dismantleGame.attackRange(dismantleB.id()) == 1 && dismantleGame.deck().discardPileSize() == discarded + 2);

        auto snatchGame = combatGame(); auto &snatchA = *snatchGame.players()[0]; auto &snatchB = *snatchGame.players()[1];
        setupEquipmentTarget(snatchGame, "qinggang-n"); snatchA.addCard(card("snatch-weapon", CardType::Snatch));
        assert(snatchGame.canPlayCardOnTarget(snatchA.id(), "snatch-weapon", snatchB.id()));
        assert(snatchGame.submitAction(PlayCardAction {snatchA.id(), "snatch-weapon", {snatchB.id()}}).accepted);
        passNullificationChain(snatchGame);
        const auto snatchRequest = *snatchGame.pendingCardSelection();
        assert(snatchGame.submitAction(SelectCardsAction {snatchA.id(), snatchRequest.requestId, {"qinggang-n"}}).accepted);
        assert(!snatchB.hasEquipment(EquipmentSlot::Weapon) && !snatchA.hasEquipment(EquipmentSlot::Weapon)
            && std::any_of(snatchA.handCards().begin(), snatchA.handCards().end(), [](const auto &card) { return card->id() == "qinggang-n"; }));
    }
    { // Stage 6 P and Q: equipment does not count as hand cards and remains action-locked by pending selection.
        auto game = combatGame(); auto &a = *game.players()[0]; auto &b = *game.players()[1];
        a.addCard(equipmentCard("p-weapon", CardType::Weapon, 2)); a.addCard(equipmentCard("p-armor", CardType::Armor));
        a.addCard(equipmentCard("p-offensive", CardType::OffensiveHorse)); a.addCard(equipmentCard("p-defensive", CardType::DefensiveHorse));
        for (const auto &id : {"p-weapon", "p-armor", "p-offensive", "p-defensive"}) assert(game.submitAction(PlayCardAction {a.id(), id, {}}).accepted);
        for (int index = 0; index < 4; ++index) a.addCard(card("p-hand-" + std::to_string(index), CardType::Slash));
        assert(game.requiredDiscardCount() == 0);
        a.addCard(card("p-dismantle", CardType::Dismantlement)); b.addCard(card("p-target", CardType::Slash));
        assert(game.submitAction(PlayCardAction {a.id(), "p-dismantle", {b.id()}}).accepted);
        passNullificationChain(game);
        assert(game.pendingCardSelection() && game.pendingCardSelection()->purpose == CardSelectionPurpose::Dismantlement);
        a.addCard(equipmentCard("p-blocked", CardType::Armor));
        assert(!game.submitAction(PlayCardAction {a.id(), "p-blocked", {}}).accepted);
    }
    { // Stage 7 A-D: client views isolate private hands and reject impersonation.
        GameSession session({"Player 1", "Player 2"});
        GameClientController client1(session, 1); GameClientController client2(session, 2);
        const auto& view1 = client1.view(); const auto& view2 = client2.view();
        assert(view1.ownHand.size() == session.testingEngine().player(1)->handCards().size());
        assert(view2.ownHand.size() == session.testingEngine().player(2)->handCards().size());
        assert(view1.players.at(1).handCardCount == session.testingEngine().player(2)->handCards().size());
        assert(view2.players.at(0).handCardCount == session.testingEngine().player(1)->handCards().size());
        const int turnBefore = view1.turnNumber;
        const auto denied = client1.submit(EndPlayPhaseAction {2});
        assert(!denied.accepted && denied.error == ActionResult::Error::UnauthorizedPlayer);
        assert(client1.view().turnNumber == turnBefore && client1.view().currentTurnPlayer == 1);
    }
    { // Stage 7 E, F and I: action propagation, response privacy and opaque hidden-card selection.
        GameSession session({"Player 1", "Player 2"});
        GameClientController client1(session, 1); GameClientController client2(session, 2);
        auto& engine = session.testingEngine(); auto& one = *engine.players()[0]; auto& two = *engine.players()[1];
        clearHand(one); clearHand(two); one.addCard(card("s7-slash", CardType::Slash)); two.addCard(card("s7-dodge", CardType::Dodge));
        client1.refresh(); client2.refresh();
        assert(client1.submit(PlayCardAction {1, "s7-slash", {2}}).accepted);
        client1.refresh(); client2.refresh();
        assert(client1.view().response && !client1.view().response->isResponder && client1.view().response->selectableCards.empty());
        assert(client2.view().response && client2.view().response->isResponder && client2.view().response->selectableCards.size() == 1);
        const auto dodgeId = client2.view().response->selectableCards.front().id;
        assert(client2.submit(RespondAction {2, client2.view().response->requestId, dodgeId}).accepted);
        client1.refresh(); client2.refresh(); assert(!client1.view().response && !client2.view().response);
        one.addCard(card("s7-dismantle", CardType::Dismantlement)); two.addCard(card("s7-hidden", CardType::Peach));
        client1.refresh(); assert(client1.submit(PlayCardAction {1, "s7-dismantle", {2}}).accepted);
        while (engine.pendingResponse()) {
            const auto request = *engine.pendingResponse();
            assert(request.type == ResponseType::Nullification);
            assert((request.responder == 1 ? client1 : client2).submit(RespondAction {request.responder, request.requestId, std::nullopt}).accepted);
        }
        client1.refresh(); client2.refresh();
        assert(client1.view().cardSelection && client1.view().cardSelection->options.size() == 1);
        assert(!client2.view().cardSelection);
        for (const auto& option : client1.view().cardSelection->options) if (option.hidden) assert(option.displayName.empty());
    }
    { // Stage 7 G: Duel response cards are visible only to the responder.
        GameSession session({"Player 1", "Player 2"});
        GameClientController client1(session, 1); GameClientController client2(session, 2);
        auto& engine = session.testingEngine(); auto& one = *engine.players()[0]; auto& two = *engine.players()[1];
        clearHand(one); clearHand(two); one.addCard(card("s7-duel", CardType::Duel)); two.addCard(card("s7-duel-slash", CardType::Slash));
        assert(client1.submit(PlayCardAction {1, "s7-duel", {2}}).accepted);
        while (engine.pendingResponse() && engine.pendingResponse()->type == ResponseType::Nullification) {
            const auto request = *engine.pendingResponse();
            assert((request.responder == 1 ? client1 : client2).submit(RespondAction {request.responder, request.requestId, std::nullopt}).accepted);
        }
        client1.refresh(); client2.refresh();
        assert(client1.view().response && client1.view().response->selectableCards.empty());
        assert(client2.view().response && client2.view().response->isResponder && client2.view().response->selectableCards.size() == 1);
        assert(client2.submit(RespondAction {2, client2.view().response->requestId, "s7-duel-slash"}).accepted);
        client1.refresh(); client2.refresh();
        assert(client1.view().response && client1.view().response->isResponder && client1.view().response->selectableCards.empty());
        assert(client2.view().response && !client2.view().response->isResponder && client2.view().response->selectableCards.empty());
        assert(client1.submit(RespondAction {1, client1.view().response->requestId, std::nullopt}).accepted);
        client1.refresh(); client2.refresh();
        assert(!client1.view().gameOver && !client2.view().gameOver);
    }
    { // Stage 7 H and K: only the rescuer receives Peach choices; game over is consistent.
        GameSession session({"Player 1", "Player 2"});
        GameClientController client1(session, 1); GameClientController client2(session, 2);
        auto& engine = session.testingEngine(); reducePlayerTwoToOneHp(engine);
        auto& one = *engine.players()[0]; auto& two = *engine.players()[1]; clearHand(one); clearHand(two); one.addCard(card("s7-final-slash", CardType::Slash));
        client1.refresh(); client2.refresh(); assert(client1.submit(PlayCardAction {1, "s7-final-slash", {2}}).accepted);
        client2.refresh(); assert(client2.submit(RespondAction {2, client2.view().response->requestId, std::nullopt}).accepted);
        client1.refresh(); client2.refresh();
        assert(client2.view().response && client2.view().response->isResponder && client2.view().response->type == ResponseType::PeachRescue && client2.view().response->selectableCards.empty());
        assert(client1.view().response && !client1.view().response->isResponder && client1.view().response->selectableCards.empty());
        assert(client2.submit(RespondAction {2, client2.view().response->requestId, std::nullopt}).accepted);
        client1.refresh(); assert(client1.view().response && client1.view().response->isResponder);
        assert(client1.submit(RespondAction {1, client1.view().response->requestId, std::nullopt}).accepted);
        client1.refresh(); client2.refresh(); assert(client1.view().gameOver && client2.view().gameOver && client1.view().winner == client2.view().winner);
    }
    { // Stage 8 protocol: length framing handles partial and coalesced JSON messages safely.
        using namespace sanguosha::network;
        const auto one = MessageCodec::frame(QJsonObject{{"type", "hello"}, {"protocolVersion", ProtocolVersion}});
        const auto two = MessageCodec::frame(QJsonObject{{"type", "welcome"}, {"player", 2}});
        MessageCodec codec; std::vector<QJsonObject> messages; QString error;
        assert(codec.append(one.left(2), messages, error) && messages.empty());
        assert(codec.append(one.mid(2) + two, messages, error) && messages.size() == 2);
        QByteArray invalid(4, '\0'); invalid[0] = char(0x7f);
        MessageCodec oversized; messages.clear(); assert(!oversized.append(invalid, messages, error));
    }
    { // Stage 8 protocol: a player-specific view and Action round-trip without opponent hand data.
        using namespace sanguosha::network;
        GameSession session({"Player 1", "Player 2"}); const auto client = session.addClient(1); const auto view = session.viewFor(client);
        const auto json = encodeViewState(view); const auto decoded = decodeViewState(json);
        assert(decoded && decoded->selfPlayerId == 1 && decoded->ownHand.size() == view.ownHand.size());
        const auto action = PlayCardAction {1, "round-trip", {2}}; const auto decodedAction = decodeAction(encodeAction(action));
        assert(decodedAction && std::get<PlayCardAction>(decodedAction->action).cardId == "round-trip");
    }
    { // Stage 12A: identity games retain seats while their assignments use the formal distribution.
        GameEngine game;
        game.createGame({"P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8"}, {}, {}, GameMode::Identity);
        assert(game.players().size() == 8);
        for (std::size_t i = 0; i < game.players().size(); ++i) {
            assert(game.players()[i]->seat() == static_cast<Seat>(i));
        }
        assert(game.nextAlivePlayer(8) == std::optional<PlayerId>(1));
        game.testingSetPlayerStatus(2, PlayerStatus::Dead);
        game.testingSetPlayerStatus(3, PlayerStatus::Dead);
        assert(game.nextAlivePlayer(1) == std::optional<PlayerId>(4));
    }
    { // Stage 9.1: explicit identities drive all three base victory outcomes.
        const std::vector<PlayerIdentity> identities {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Renegade, PlayerIdentity::Rebel};
        GameEngine lordGame; lordGame.createGame({"L", "F", "R", "N", "R2"}, identities, {}, GameMode::Identity);
        lordGame.testingSetPlayerStatus(3, PlayerStatus::Dead); lordGame.testingSetPlayerStatus(4, PlayerStatus::Dead); lordGame.testingSetPlayerStatus(5, PlayerStatus::Dead); lordGame.testingCheckGameOver();
        assert(lordGame.gameOver() && lordGame.winningSide() == WinningSide::LordSide && lordGame.winningPlayers().size() == 2);
        GameEngine rebelGame; rebelGame.createGame({"L", "F", "R", "N", "R2"}, identities, {}, GameMode::Identity);
        rebelGame.testingSetPlayerStatus(1, PlayerStatus::Dead); rebelGame.testingCheckGameOver();
        assert(rebelGame.gameOver() && rebelGame.winningSide() == WinningSide::RebelSide && rebelGame.winner() == std::optional<PlayerId>(3));
        GameEngine renegadeGame; renegadeGame.createGame({"L", "F", "R", "N", "R2"}, identities, {}, GameMode::Identity);
        renegadeGame.testingSetPlayerStatus(1, PlayerStatus::Dead); renegadeGame.testingSetPlayerStatus(2, PlayerStatus::Dead); renegadeGame.testingSetPlayerStatus(3, PlayerStatus::Dead); renegadeGame.testingSetPlayerStatus(5, PlayerStatus::Dead); renegadeGame.testingCheckGameOver();
        assert(renegadeGame.gameOver() && renegadeGame.winningSide() == WinningSide::Renegade && renegadeGame.winner() == std::optional<PlayerId>(4));
    }
    { // Stage 9.1: the view exposes only self, Lord and dead identities.
        const std::vector<PlayerIdentity> identities {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Renegade, PlayerIdentity::Rebel};
        GameSession session({"L", "F", "R", "N", "R2"}, true, identities, {}, GameMode::Identity);
        const auto client = session.addClient(3);
        const auto view = session.viewFor(client);
        assert(view.selfIdentity == PlayerIdentity::Rebel && view.players[0].identity == PlayerIdentity::Lord);
        assert(!view.players[1].identity && view.players[2].identity == PlayerIdentity::Rebel && !view.players[3].identity);
        session.testingEngine().testingSetPlayerStatus(2, PlayerStatus::Dead);
        const auto afterDeath = session.viewFor(client);
        assert(afterDeath.players[1].identity == PlayerIdentity::Loyalist);
    }
    { // Stage 9.2: eight seats advance in order, wrap, and skip every dead seat.
        GameEngine game; game.createGame({"P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8"}, {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Renegade}, {}, GameMode::Identity); game.startGame();
        for (const auto& player : game.players()) clearHand(*player);
        std::vector<PlayerId> order {game.currentPlayer()->id()};
        for (int turn = 0; turn < 8; ++turn) { finishCurrentTurn(game); clearHand(*game.players()[game.currentPlayer()->id() - 1]); order.push_back(game.currentPlayer()->id()); }
        assert((order == std::vector<PlayerId>{1,2,3,4,5,6,7,8,1}));
        game.testingSetPlayerStatus(2, PlayerStatus::Dead); game.testingSetPlayerStatus(3, PlayerStatus::Dead); game.testingSetPlayerStatus(6, PlayerStatus::Dead);
        assert(game.nextAlivePlayer(1) == std::optional<PlayerId>(4));
        assert(game.nextAlivePlayer(5) == std::optional<PlayerId>(7));
        assert(game.nextAlivePlayer(8) == std::optional<PlayerId>(1));
    }
    { // Stage 9.2: distance uses the alive-seat ring and Slash uses its live weapon range.
        GameEngine game; game.createGame({"P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8"}, {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Renegade}, {}, GameMode::Identity); game.startGame();
        for (const auto& player : game.players()) clearHand(*player);
        auto& p1 = *game.players()[0]; auto& p2 = *game.players()[1]; auto& p3 = *game.players()[2]; auto& p5 = *game.players()[4]; auto& p8 = *game.players()[7];
        assert(game.distanceBetween(p1.id(), p2.id()) == 1 && game.distanceBetween(p1.id(), p3.id()) == 2);
        assert(game.distanceBetween(p1.id(), p5.id()) == 4 && game.distanceBetween(p1.id(), p8.id()) == 1);
        assert(game.distanceBetween(p3.id(), p8.id()) == game.distanceBetween(p8.id(), p3.id()));
        game.testingSetPlayerStatus(p2.id(), PlayerStatus::Dead); assert(game.distanceBetween(p1.id(), p3.id()) == 1);
        game.testingSetPlayerStatus(p3.id(), PlayerStatus::Dead); assert(game.distanceBetween(p1.id(), game.players()[3]->id()) == 1);
        p1.addCard(card("stage92-slash", CardType::Slash)); p1.addCard(equipmentCard("stage92-range2", CardType::Weapon, 2));
        assert(game.attackRange(p1.id()) == 1 && !game.canPlayCardOnTarget(p1.id(), "stage92-slash", p5.id()));
        assert(game.submitAction(PlayCardAction {p1.id(), "stage92-range2", {}}).accepted);
        assert(game.attackRange(p1.id()) == 2 && game.canPlayCardOnTarget(p1.id(), "stage92-slash", p5.id()));
        assert(!game.canPlayCardOnTarget(p1.id(), "stage92-slash", p1.id()));
        game.testingSetPlayerStatus(p5.id(), PlayerStatus::Dead);
        assert(!game.canPlayCardOnTarget(p1.id(), "stage92-slash", p5.id()));
    }
    { // Stage 12A: FreeForAll ends only when a sole survivor remains.
        GameEngine game; game.createGame({"P1", "P2", "P3", "P4"}); game.startGame();
        const auto current = game.currentPlayer()->id();
        game.testingSetPlayerStatus(2, PlayerStatus::Dead); game.testingSetPlayerStatus(3, PlayerStatus::Dead); game.testingSetPlayerStatus(4, PlayerStatus::Dead); game.testingCheckGameOver();
        assert(game.gameOver() && game.currentPlayer()->id() == current && game.winningSide() == WinningSide::FreeForAll && game.winner() == std::optional<PlayerId>(1));
    }
    { // Stage 9.2: an eliminated active player immediately yields to the next alive seat when the game continues.
        const std::vector<PlayerIdentity> identities {PlayerIdentity::Rebel, PlayerIdentity::Lord, PlayerIdentity::Rebel, PlayerIdentity::Renegade};
        GameEngine game; game.createGame({"P1", "P2", "P3", "P4"}, identities); game.startGame();
        game.testingKillPlayer(1);
        assert(!game.gameOver() && game.currentPlayer()->id() == 2 && game.currentPhase() == Phase::Play);
    }
    { // B3: Identity mode has one authoritative distribution for every supported player count and Lord opens.
        const std::array<IdentityDistribution, 4> expected {{{1,1,2,1}, {1,1,3,1}, {1,2,3,1}, {1,2,4,1}}};
        for (int count = 5; count <= 8; ++count) {
            const auto distribution = identityDistributionForPlayerCount(count);
            assert(distribution.lordCount == expected[count - 5].lordCount && distribution.loyalistCount == expected[count - 5].loyalistCount
                   && distribution.rebelCount == expected[count - 5].rebelCount && distribution.renegadeCount == expected[count - 5].renegadeCount);
            std::vector<std::string> names; for (int id = 1; id <= count; ++id) names.push_back("P" + std::to_string(id));
            GameEngine game; game.createGame(names, {}, {}, GameMode::Identity); game.startGame();
            int lord = 0, loyalist = 0, rebel = 0, renegade = 0;
            for (const auto& player : game.players()) {
                lord += player->identity() == PlayerIdentity::Lord; loyalist += player->identity() == PlayerIdentity::Loyalist;
                rebel += player->identity() == PlayerIdentity::Rebel; renegade += player->identity() == PlayerIdentity::Renegade;
            }
            assert(lord == distribution.lordCount && loyalist == distribution.loyalistCount && rebel == distribution.rebelCount && renegade == distribution.renegadeCount);
            assert(game.currentPlayer()->identity() == PlayerIdentity::Lord && game.gameMode() == GameMode::Identity);
        }
        bool rejected = false; try { GameEngine game; game.createGame({"P1", "P2", "P3", "P4"}, {}, {}, GameMode::Identity); } catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }
    { // B4.2: one authoritative player-count matrix drives every lobby start.
        for (int count = 2; count <= 4; ++count) assert(gameModeForPlayerCount(count) == GameMode::FreeForAll);
        for (int count = 5; count <= 8; ++count) assert(gameModeForPlayerCount(count) == GameMode::Identity);
    }
    { // Stage 12A: FreeForAll has neither real nor exposed role assignments.
        GameSession session({"A", "B", "C", "D"}, true, {}, {}, GameMode::FreeForAll);
        const auto view = session.viewFor(session.addClient(2));
        assert(view.gameMode == GameMode::FreeForAll && view.selfIdentity == PlayerIdentity::None);
        assert(std::all_of(view.players.begin(), view.players.end(), [](const auto& player) { return !player.identity.has_value(); }));
        for (const auto& player : session.testingEngine().players()) assert(player->identity() == PlayerIdentity::None);
    }
    { // B3: explicit identity endgames retain Lord/Loyalist, Rebel and Renegade victory distinctions.
        const std::vector<PlayerIdentity> identities {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Renegade, PlayerIdentity::Rebel};
        GameEngine lordGame; lordGame.createGame({"L","F","R","N","R2"}, identities, {}, GameMode::Identity);
        lordGame.testingSetPlayerStatus(3, PlayerStatus::Dead); lordGame.testingSetPlayerStatus(4, PlayerStatus::Dead); lordGame.testingSetPlayerStatus(5, PlayerStatus::Dead); lordGame.testingCheckGameOver();
        assert(lordGame.winningSide() == WinningSide::LordSide);
        GameEngine rebelGame; rebelGame.createGame({"L","F","R","N","R2"}, identities, {}, GameMode::Identity);
        rebelGame.testingSetPlayerStatus(1, PlayerStatus::Dead); rebelGame.testingCheckGameOver(); assert(rebelGame.winningSide() == WinningSide::RebelSide);
        GameEngine renegadeGame; renegadeGame.createGame({"L","F","R","N","R2"}, identities, {}, GameMode::Identity);
        renegadeGame.testingSetPlayerStatus(2, PlayerStatus::Dead); renegadeGame.testingSetPlayerStatus(3, PlayerStatus::Dead); renegadeGame.testingSetPlayerStatus(5, PlayerStatus::Dead); renegadeGame.testingSetPlayerStatus(1, PlayerStatus::Dead); renegadeGame.testingCheckGameOver();
        assert(renegadeGame.winningSide() == WinningSide::Renegade);
    }
    { // B3: killer context survives PeachRescue and applies each identity reward or penalty exactly once.
        const std::vector<PlayerIdentity> identities {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Renegade, PlayerIdentity::Rebel};
        const auto clearAll = [](GameEngine& game) { for (const auto& player : game.players()) clearHand(*player); };
        GameEngine reward; reward.createGame({"L","F","R","N","R2"}, identities, {}, GameMode::Identity); reward.startGame(); clearAll(reward);
        reward.testingSetHp(5, 1); reward.players()[0]->addCard(card("b3-reward-slash", CardType::Slash));
        assert(reward.submitAction(PlayCardAction {1, "b3-reward-slash", {5}}).accepted); auto response = *reward.pendingResponse(); assert(reward.submitAction(RespondAction {5, response.requestId, std::nullopt}).accepted);
        declineAllRescues(reward); assert(reward.players()[4]->status() == PlayerStatus::Dead && reward.players()[0]->handCards().size() == 3);
        const auto rewardHand = reward.players()[0]->handCards().size(); reward.testingKillPlayer(5); assert(reward.players()[0]->handCards().size() == rewardHand);
        GameEngine penalty; penalty.createGame({"L","F","R","N","R2"}, identities, {}, GameMode::Identity); penalty.startGame(); clearAll(penalty);
        penalty.testingSetHp(2, 1); penalty.players()[0]->addCard(card("b3-penalty-slash", CardType::Slash)); penalty.players()[0]->addCard(card("b3-lord-hand", CardType::Peach));
        penalty.testingEquip(1, equipmentCard("b3-lord-weapon", CardType::Weapon, 2));
        assert(penalty.submitAction(PlayCardAction {1, "b3-penalty-slash", {2}}).accepted); response = *penalty.pendingResponse(); assert(penalty.submitAction(RespondAction {2, response.requestId, std::nullopt}).accepted);
        declineAllRescues(penalty); assert(penalty.players()[0]->handCards().empty() && !penalty.players()[0]->equipment(EquipmentSlot::Weapon));
        GameEngine nonLord; nonLord.createGame({"F","L","R","F2","N"}, {PlayerIdentity::Loyalist, PlayerIdentity::Lord, PlayerIdentity::Rebel, PlayerIdentity::Loyalist, PlayerIdentity::Renegade}, {}, GameMode::Identity); nonLord.startGame(); clearAll(nonLord);
        assert(nonLord.submitAction(EndPlayPhaseAction {2}).accepted); assert(nonLord.currentPlayer()->id() == 3);
        nonLord.testingSetHp(4, 1); nonLord.players()[2]->addCard(card("b3-nonlord-slash", CardType::Slash)); nonLord.players()[2]->addCard(card("b3-nonlord-hand", CardType::Peach));
        assert(nonLord.submitAction(PlayCardAction {3, "b3-nonlord-slash", {4}}).accepted); response = *nonLord.pendingResponse(); assert(nonLord.submitAction(RespondAction {4, response.requestId, std::nullopt}).accepted);
        declineAllRescues(nonLord); assert(!nonLord.players()[2]->handCards().empty());
    }
    { // B4.1: the dying player may use Wine only for self-rescue; other rescuers still require Peach.
        auto selfRescue = combatGame(); auto& source = *selfRescue.players()[0]; auto& dying = *selfRescue.players()[1];
        selfRescue.testingSetHp(dying.id(), 1);
        source.addCard(card("b41-self-rescue-slash", CardType::Slash)); dying.addCard(card("b41-self-rescue-wine", CardType::Wine));
        assert(selfRescue.submitAction(PlayCardAction {source.id(), "b41-self-rescue-slash", {dying.id()}}).accepted);
        auto response = *selfRescue.pendingResponse(); assert(selfRescue.submitAction(RespondAction {dying.id(), response.requestId, std::nullopt}).accepted);
        response = *selfRescue.pendingResponse(); assert(response.type == ResponseType::PeachRescue && response.responder == dying.id());
        assert(selfRescue.submitAction(RespondAction {dying.id(), response.requestId, CardId("b41-self-rescue-wine")}).accepted);
        assert(dying.isAlive() && dying.hp() == 1 && !selfRescue.pendingResponse());

        auto otherRescue = combatGame(); auto& other = *otherRescue.players()[0]; auto& otherDying = *otherRescue.players()[1];
        otherRescue.testingSetHp(otherDying.id(), 1); other.addCard(card("b41-other-slash", CardType::Slash)); other.addCard(card("b41-illegal-wine", CardType::Wine));
        assert(otherRescue.submitAction(PlayCardAction {other.id(), "b41-other-slash", {otherDying.id()}}).accepted);
        response = *otherRescue.pendingResponse(); assert(otherRescue.submitAction(RespondAction {otherDying.id(), response.requestId, std::nullopt}).accepted);
        response = *otherRescue.pendingResponse(); assert(response.responder == otherDying.id());
        assert(otherRescue.submitAction(RespondAction {otherDying.id(), response.requestId, std::nullopt}).accepted);
        response = *otherRescue.pendingResponse(); assert(response.responder == other.id());
        assert(!otherRescue.submitAction(RespondAction {other.id(), response.requestId, CardId("b41-illegal-wine")}).accepted);
    }
    { // B4.1: the repeating crossbow is a data-driven weapon exception to the normal one-Slash limit.
        auto game = combatGame(); auto& source = *game.players()[0]; auto& target = *game.players()[1];
        game.testingEquip(source.id(), std::make_shared<Card>("b41-crossbow", "诸葛连弩", CardType::Weapon, Suit::Club, 1,
            EquipmentData {EquipmentSlot::Weapon, 1, true}));
        source.addCard(card("b41-crossbow-slash-1", CardType::Slash)); source.addCard(card("b41-crossbow-slash-2", CardType::Slash));
        assert(game.attackRange(source.id()) == 1);
        assert(game.submitAction(PlayCardAction {source.id(), "b41-crossbow-slash-1", {target.id()}}).accepted);
        auto response = *game.pendingResponse(); assert(game.submitAction(RespondAction {target.id(), response.requestId, std::nullopt}).accepted);
        assert(game.submitAction(PlayCardAction {source.id(), "b41-crossbow-slash-2", {target.id()}}).accepted);
    }
    { // B4.2: Barbarian Invasion resolves living seats in order and keeps the source Play phase after completion.
        auto game = fourPlayerGame(); auto& p1 = *game.players()[0]; auto& p2 = *game.players()[1]; auto& p3 = *game.players()[2]; auto& p4 = *game.players()[3];
        p1.addCard(card("b42-barbarian", CardType::BarbarianInvasion)); p2.addCard(card("b42-response-slash", CardType::Slash));
        assert(game.submitAction(PlayCardAction {p1.id(), "b42-barbarian", {}}).accepted);
        passNullificationChain(game);
        auto request = *game.pendingResponse(); assert(request.responder == p2.id() && request.type == ResponseType::Slash);
        assert(game.submitAction(RespondAction {p2.id(), request.requestId, CardId("b42-response-slash")}).accepted);
        passNullificationChain(game);
        request = *game.pendingResponse(); assert(request.responder == p3.id()); assert(game.submitAction(RespondAction {p3.id(), request.requestId, std::nullopt}).accepted);
        passNullificationChain(game);
        request = *game.pendingResponse(); assert(request.responder == p4.id()); const auto oldRequest = request.requestId;
        assert(game.handleTimeout().accepted && p3.hp() == 3 && p4.hp() == 3);
        assert(!game.submitAction(RespondAction {p4.id(), oldRequest, std::nullopt}).accepted);
        assert(!game.pendingResponse() && !game.multiTargetEffect() && game.currentPlayer()->id() == p1.id() && game.currentPhase() == Phase::Play);
    }
    { // B4.2: Arrow Barrage requires Dodge; Peach Garden heals wounded living players only.
        auto arrow = fourPlayerGame(); auto& p1 = *arrow.players()[0]; auto& p2 = *arrow.players()[1]; auto& p3 = *arrow.players()[2]; auto& p4 = *arrow.players()[3];
        p1.addCard(card("b42-arrow", CardType::ArrowBarrage)); p2.addCard(card("b42-dodge", CardType::Dodge));
        assert(arrow.submitAction(PlayCardAction {p1.id(), "b42-arrow", {}}).accepted); passNullificationChain(arrow); auto request = *arrow.pendingResponse(); assert(request.type == ResponseType::Dodge && request.responder == p2.id());
        assert(arrow.submitAction(RespondAction {p2.id(), request.requestId, CardId("b42-dodge")}).accepted); passNullificationChain(arrow); request = *arrow.pendingResponse(); assert(request.responder == p3.id()); assert(arrow.submitAction(RespondAction {p3.id(), request.requestId, std::nullopt}).accepted); assert(p3.hp() == 3);
        passNullificationChain(arrow);
        request = *arrow.pendingResponse(); assert(request.responder == p4.id()); assert(arrow.handleTimeout().accepted && p4.hp() == 3);
        auto garden = fourPlayerGame(); auto& g1 = *garden.players()[0]; auto& g2 = *garden.players()[1]; auto& g3 = *garden.players()[2]; auto& g4 = *garden.players()[3];
        garden.testingSetHp(g1.id(), 2); garden.testingSetHp(g4.id(), 1); garden.testingSetPlayerStatus(g3.id(), PlayerStatus::Dead); g1.addCard(card("b42-garden", CardType::PeachGarden));
        assert(garden.submitAction(PlayCardAction {g1.id(), "b42-garden", {}}).accepted); passNullificationChain(garden); assert(g1.hp() == 3 && g2.hp() == 4 && g3.status() == PlayerStatus::Dead && g4.hp() == 2 && garden.currentPhase() == Phase::Play);
    }
    { // B4.2 seal: Barbarian Invasion pauses for Peach rescue, then resumes at the next untouched target.
        auto game = fourPlayerGame(); auto& p1 = *game.players()[0]; auto& p2 = *game.players()[1]; auto& p3 = *game.players()[2];
        game.testingSetHp(p2.id(), 1); p1.addCard(card("b42-rescue-barbarian", CardType::BarbarianInvasion)); p3.addCard(card("b42-rescue-peach", CardType::Peach));
        assert(game.submitAction(PlayCardAction {p1.id(), "b42-rescue-barbarian", {}}).accepted);
        passNullificationChain(game);
        auto request = *game.pendingResponse(); assert(request.responder == p2.id() && request.type == ResponseType::Slash);
        assert(game.submitAction(RespondAction {p2.id(), request.requestId, std::nullopt}).accepted && p2.status() == PlayerStatus::Dying && game.multiTargetEffect());
        request = *game.pendingResponse(); assert(request.type == ResponseType::PeachRescue && request.responder == p2.id());
        assert(game.submitAction(RespondAction {p2.id(), request.requestId, std::nullopt}).accepted);
        request = *game.pendingResponse(); assert(request.type == ResponseType::PeachRescue && request.responder == p3.id()); const auto rescueRequest = request.requestId;
        assert(game.submitAction(RespondAction {p3.id(), request.requestId, CardId("b42-rescue-peach")}).accepted);
        passNullificationChain(game);
        request = *game.pendingResponse(); assert(p2.isAlive() && !game.dyingContext() && game.multiTargetEffect() && request.responder == p3.id() && request.requestId != rescueRequest && request.type == ResponseType::Slash);
    }
    { // B4.2 seal: a dead AOE target is skipped and later living targets continue exactly once.
        auto game = fourPlayerGame(); auto& p1 = *game.players()[0]; auto& p2 = *game.players()[1]; auto& p3 = *game.players()[2]; auto& p4 = *game.players()[3];
        game.testingSetHp(p2.id(), 1); p1.addCard(card("b42-death-barbarian", CardType::BarbarianInvasion));
        assert(game.submitAction(PlayCardAction {p1.id(), "b42-death-barbarian", {}}).accepted);
        passNullificationChain(game);
        auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {p2.id(), request.requestId, std::nullopt}).accepted);
        while (game.pendingResponse() && game.pendingResponse()->type == ResponseType::PeachRescue) { request = *game.pendingResponse(); assert(game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        passNullificationChain(game);
        request = *game.pendingResponse(); assert(p2.status() == PlayerStatus::Dead && game.multiTargetEffect() && request.responder == p3.id() && request.type == ResponseType::Slash);
        assert(game.submitAction(RespondAction {p3.id(), request.requestId, std::nullopt}).accepted && p3.hp() == 3);
        passNullificationChain(game);
        request = *game.pendingResponse(); assert(request.responder == p4.id());
    }
    { // B4.2 seal: Arrow Barrage shares the Dying/Peach-resume state machine.
        auto game = fourPlayerGame(); auto& p1 = *game.players()[0]; auto& p2 = *game.players()[1]; auto& p3 = *game.players()[2];
        game.testingSetHp(p2.id(), 1); p1.addCard(card("b42-rescue-arrow", CardType::ArrowBarrage)); p3.addCard(card("b42-arrow-peach", CardType::Peach));
        assert(game.submitAction(PlayCardAction {p1.id(), "b42-rescue-arrow", {}}).accepted);
        passNullificationChain(game);
        auto request = *game.pendingResponse(); assert(request.type == ResponseType::Dodge && request.responder == p2.id());
        assert(game.submitAction(RespondAction {p2.id(), request.requestId, std::nullopt}).accepted);
        request = *game.pendingResponse(); assert(request.type == ResponseType::PeachRescue && request.responder == p2.id());
        assert(game.submitAction(RespondAction {p2.id(), request.requestId, std::nullopt}).accepted);
        request = *game.pendingResponse(); assert(request.responder == p3.id() && request.type == ResponseType::PeachRescue);
        assert(game.submitAction(RespondAction {p3.id(), request.requestId, CardId("b42-arrow-peach")}).accepted);
        passNullificationChain(game);
        request = *game.pendingResponse(); assert(p2.isAlive() && request.responder == p3.id() && request.type == ResponseType::Dodge);
    }
    { // B4.2 seal: AOE killing a Rebel keeps attribution and grants exactly three cards.
        GameEngine game; game.createGame({"Lord", "Loyalist", "Rebel", "Rebel2", "Renegade"}, {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Renegade}, {}, GameMode::Identity); game.startGame();
        auto& lord = *game.players()[0]; auto& loyalist = *game.players()[1]; auto& rebel = *game.players()[2]; for (const auto& player : game.players()) clearHand(*player);
        lord.addCard(card("b42-reward-barbarian", CardType::BarbarianInvasion)); loyalist.addCard(card("b42-reward-pass", CardType::Slash)); game.testingSetHp(rebel.id(), 1);
        assert(game.submitAction(PlayCardAction {lord.id(), "b42-reward-barbarian", {}}).accepted); passNullificationChain(game); auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {loyalist.id(), request.requestId, CardId("b42-reward-pass")}).accepted);
        passNullificationChain(game);
        request = *game.pendingResponse(); assert(request.responder == rebel.id() && game.submitAction(RespondAction {rebel.id(), request.requestId, std::nullopt}).accepted);
        while (rebel.status() != PlayerStatus::Dead) { request = *game.pendingResponse(); assert(request.type == ResponseType::PeachRescue && game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        assert(rebel.status() == PlayerStatus::Dead && lord.handCards().size() == 3);
    }
    { // B4.2 seal: Lord AOE killing a Loyalist applies the hand-and-equipment penalty once.
        GameEngine game; game.createGame({"Lord", "Loyalist", "Rebel", "Rebel2", "Renegade"}, {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Renegade}, {}, GameMode::Identity); game.startGame();
        auto& lord = *game.players()[0]; auto& loyalist = *game.players()[1]; for (const auto& player : game.players()) clearHand(*player);
        lord.addCard(card("b42-penalty-barbarian", CardType::BarbarianInvasion)); lord.addCard(card("b42-penalty-hand", CardType::Peach)); game.testingEquip(lord.id(), equipmentCard("b42-penalty-weapon", CardType::Weapon, 2)); game.testingSetHp(loyalist.id(), 1);
        assert(game.submitAction(PlayCardAction {lord.id(), "b42-penalty-barbarian", {}}).accepted); passNullificationChain(game); auto request = *game.pendingResponse(); assert(request.responder == loyalist.id() && game.submitAction(RespondAction {loyalist.id(), request.requestId, std::nullopt}).accepted);
        while (loyalist.status() != PlayerStatus::Dead) { request = *game.pendingResponse(); assert(request.type == ResponseType::PeachRescue && game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        assert(loyalist.status() == PlayerStatus::Dead && lord.handCards().empty() && !lord.equipment(EquipmentSlot::Weapon));
    }
    { // B4.3A: Harvest starts with its source, exposes a server-owned pool, and rejects stale/non-picker selections.
        auto game = fourPlayerGame(); auto& p1 = *game.players()[0];
        p1.addCard(card("b43-harvest", CardType::Harvest));
        assert(game.submitAction(PlayCardAction {p1.id(), "b43-harvest", {}}).accepted);
        passNullificationChain(game);
        assert(game.harvestContext() && game.harvestContext()->targetOrder.front() == p1.id());
        const auto first = *game.pendingCardSelection(); assert(first.purpose == CardSelectionPurpose::Harvest && first.requester == p1.id());
        const auto firstCard = first.selectableCards.front().cardId;
        assert(!game.submitAction(SelectCardsAction {2, first.requestId, {firstCard}}).accepted);
        assert(game.submitAction(SelectCardsAction {1, first.requestId, {firstCard}}).accepted);
        assert(game.harvestContext() && game.harvestContext()->choices.size() == 1
               && game.harvestContext()->choices.front().playerId == 1
               && game.harvestContext()->choices.front().card
               && game.harvestContext()->choices.front().card->id() == firstCard
               && std::none_of(game.harvestContext()->pool.begin(), game.harvestContext()->pool.end(), [&firstCard](const auto& c) { return c->id() == firstCard; }));
        passNullificationChain(game);
        const auto second = *game.pendingCardSelection(); assert(second.requester == 2 && second.requestId != first.requestId);
        assert(!game.submitAction(SelectCardsAction {1, first.requestId, {second.selectableCards.front().cardId}}).accepted);
        assert(game.handleTimeout().accepted);
        while (game.pendingResponse() || game.pendingCardSelection()) {
            if (game.pendingResponse()) {
                const auto response = *game.pendingResponse();
                assert(response.type == ResponseType::Nullification && game.submitAction(RespondAction {response.responder, response.requestId, std::nullopt}).accepted);
            } else assert(game.handleTimeout().accepted);
        }
        assert(!game.harvestContext() && game.currentPlayer()->id() == 1 && game.currentPhase() == Phase::Play);
        assert(std::any_of(game.logEntries().begin(), game.logEntries().end(), [](const auto& entry) {
            return entry.find(" obtained [") != std::string::npos && entry.find(" from [Harvest].") != std::string::npos;
        }));
    }
    { // B4.3B: a declined forced Slash transfers the same equipped weapon to the source.
        auto game = fourPlayerGame(); auto& p1 = *game.players()[0]; auto& p2 = *game.players()[1]; auto& p3 = *game.players()[2];
        p1.addCard(card("b43-borrow", CardType::BorrowedSword)); game.testingEquip(p2.id(), equipmentCard("b43-weapon", CardType::Weapon, 3));
        assert(game.submitAction(PlayCardAction {p1.id(), "b43-borrow", {p2.id(), p3.id()}}).accepted);
        passNullificationChain(game);
        const auto request = *game.pendingResponse(); assert(request.type == ResponseType::BorrowedSwordSlash && request.responder == p2.id());
        assert(!game.submitAction(RespondAction {p1.id(), request.requestId, std::nullopt}).accepted);
        assert(game.submitAction(RespondAction {p2.id(), request.requestId, std::nullopt}).accepted);
        assert(!p2.equipment(EquipmentSlot::Weapon) && std::find_if(p1.handCards().begin(), p1.handCards().end(), [](const auto& c){ return c->id() == "b43-weapon"; }) != p1.handCards().end());
        assert(!game.borrowedSwordContext() && game.currentPhase() == Phase::Play);
    }
    { // B4.3B: forced Slash uses the normal Dodge path and leaves the weapon with its holder.
        auto game = fourPlayerGame(); auto& p1 = *game.players()[0]; auto& p2 = *game.players()[1]; auto& p3 = *game.players()[2];
        p1.addCard(card("b43-borrow-dodge", CardType::BorrowedSword)); p2.addCard(card("b43-forced-slash", CardType::Slash)); p3.addCard(card("b43-forced-dodge", CardType::Dodge));
        game.testingEquip(p2.id(), equipmentCard("b43-kept-weapon", CardType::Weapon, 3));
        assert(game.submitAction(PlayCardAction {p1.id(), "b43-borrow-dodge", {p2.id(), p3.id()}}).accepted);
        passNullificationChain(game);
        auto request = *game.pendingResponse(); assert(request.type == ResponseType::BorrowedSwordSlash && request.responder == p2.id());
        assert(game.submitAction(RespondAction {p2.id(), request.requestId, CardId("b43-forced-slash")}).accepted);
        request = *game.pendingResponse(); assert(request.type == ResponseType::Dodge && request.responder == p3.id());
        assert(game.submitAction(RespondAction {p3.id(), request.requestId, CardId("b43-forced-dodge")}).accepted);
        assert(p3.hp() == 4 && p2.equipment(EquipmentSlot::Weapon) && p2.equipment(EquipmentSlot::Weapon)->id() == "b43-kept-weapon");
        assert(!game.borrowedSwordContext() && !game.pendingResponse() && game.currentPlayer()->id() == p1.id() && game.currentPhase() == Phase::Play);
    }
    { // B4.3B: forced Kill pauses for Peach rescue, then returns to the trick source without transferring the weapon.
        auto game = fourPlayerGame(); auto& p1 = *game.players()[0]; auto& p2 = *game.players()[1]; auto& p3 = *game.players()[2]; auto& p4 = *game.players()[3];
        p1.addCard(card("b43-borrow-rescue", CardType::BorrowedSword)); p2.addCard(card("b43-rescue-slash", CardType::Slash)); p4.addCard(card("b43-rescue-peach", CardType::Peach)); game.testingEquip(p2.id(), equipmentCard("b43-rescue-weapon", CardType::Weapon, 3)); game.testingSetHp(p3.id(), 1);
        assert(game.submitAction(PlayCardAction {p1.id(), "b43-borrow-rescue", {p2.id(), p3.id()}}).accepted);
        passNullificationChain(game);
        auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {p2.id(), request.requestId, CardId("b43-rescue-slash")}).accepted);
        request = *game.pendingResponse(); assert(request.type == ResponseType::Dodge && request.responder == p3.id()); assert(game.submitAction(RespondAction {p3.id(), request.requestId, std::nullopt}).accepted);
        request = *game.pendingResponse(); assert(request.type == ResponseType::PeachRescue && request.responder == p3.id()); assert(game.submitAction(RespondAction {p3.id(), request.requestId, std::nullopt}).accepted);
        request = *game.pendingResponse(); assert(request.type == ResponseType::PeachRescue && request.responder == p4.id()); assert(game.submitAction(RespondAction {p4.id(), request.requestId, CardId("b43-rescue-peach")}).accepted);
        assert(p3.isAlive() && p3.hp() == 1 && p2.equipment(EquipmentSlot::Weapon) && p2.equipment(EquipmentSlot::Weapon)->id() == "b43-rescue-weapon");
        assert(!game.borrowedSwordContext() && !game.pendingResponse() && game.currentPlayer()->id() == p1.id() && game.currentPhase() == Phase::Play);
    }
    { // B4.3B identity: the forced Slash killer, not the trick source, earns the Rebel reward exactly once.
        const std::vector<PlayerIdentity> identities {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Renegade};
        GameEngine game; game.createGame({"L", "H", "R", "R2", "N"}, identities, {}, GameMode::Identity); game.startGame();
        for (const auto& player : game.players()) clearHand(*player);
        auto& source = *game.players()[0]; auto& holder = *game.players()[1]; auto& rebel = *game.players()[2];
        source.addCard(card("b43-identity-reward", CardType::BorrowedSword)); holder.addCard(card("b43-identity-kill", CardType::Slash)); game.testingEquip(holder.id(), equipmentCard("b43-identity-weapon", CardType::Weapon, 3)); game.testingSetHp(rebel.id(), 1);
        assert(game.submitAction(PlayCardAction {source.id(), "b43-identity-reward", {holder.id(), rebel.id()}}).accepted);
        passNullificationChain(game);
        auto request = *game.pendingResponse(); assert(request.type == ResponseType::BorrowedSwordSlash && request.responder == holder.id());
        assert(game.submitAction(RespondAction {holder.id(), request.requestId, CardId("b43-identity-kill")}).accepted);
        request = *game.pendingResponse(); assert(request.type == ResponseType::Dodge && request.responder == rebel.id()); assert(game.submitAction(RespondAction {rebel.id(), request.requestId, std::nullopt}).accepted);
        declineAllRescues(game);
        assert(rebel.status() == PlayerStatus::Dead && holder.handCards().size() == 3 && source.handCards().empty());
        assert(!game.borrowedSwordContext() && !game.pendingResponse() && game.currentPlayer()->id() == source.id() && game.currentPhase() == Phase::Play);
    }
    { // B4.3B identity: a Lord forced to kill a Loyalist receives the normal Lord penalty, never transfers the weapon.
        const std::vector<PlayerIdentity> identities {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Renegade};
        GameEngine game; game.createGame({"L", "S", "R", "R2", "N"}, identities, {}, GameMode::Identity); game.startGame();
        for (const auto& player : game.players()) clearHand(*player);
        finishCurrentTurn(game); assert(game.currentPlayer()->id() == 2 && game.currentPhase() == Phase::Play);
        auto& lord = *game.players()[0]; auto& source = *game.players()[1];
        lord.addCard(card("b43-lord-kill", CardType::Slash)); lord.addCard(card("b43-lord-extra", CardType::Peach)); game.testingEquip(lord.id(), equipmentCard("b43-lord-weapon", CardType::Weapon, 3)); source.addCard(card("b43-lord-borrow", CardType::BorrowedSword)); game.testingSetHp(source.id(), 1);
        assert(game.submitAction(PlayCardAction {source.id(), "b43-lord-borrow", {lord.id(), source.id()}}).accepted);
        passNullificationChain(game);
        auto request = *game.pendingResponse(); assert(request.type == ResponseType::BorrowedSwordSlash && request.responder == lord.id()); assert(game.submitAction(RespondAction {lord.id(), request.requestId, CardId("b43-lord-kill")}).accepted);
        request = *game.pendingResponse(); assert(request.type == ResponseType::Dodge && request.responder == source.id()); assert(game.submitAction(RespondAction {source.id(), request.requestId, std::nullopt}).accepted);
        declineAllRescues(game);
        assert(source.status() == PlayerStatus::Dead && lord.handCards().empty() && !lord.equipment(EquipmentSlot::Weapon));
        assert(!game.borrowedSwordContext() && !game.pendingResponse() && !game.gameOver());
    }
    { // B4.3B terminal: a forced Kill that kills the Lord clears all Borrowed Sword state and leaves the terminal winner intact.
        const std::vector<PlayerIdentity> identities {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Renegade};
        GameEngine game; game.createGame({"L", "H", "S", "R", "N"}, identities, {}, GameMode::Identity); game.startGame();
        for (const auto& player : game.players()) clearHand(*player);
        finishCurrentTurn(game); finishCurrentTurn(game); assert(game.currentPlayer()->id() == 3 && game.currentPhase() == Phase::Play);
        auto& lord = *game.players()[0]; auto& holder = *game.players()[1]; auto& source = *game.players()[2];
        holder.addCard(card("b43-terminal-kill", CardType::Slash)); game.testingEquip(holder.id(), equipmentCard("b43-terminal-weapon", CardType::Weapon, 3)); source.addCard(card("b43-terminal-borrow", CardType::BorrowedSword)); game.testingSetHp(lord.id(), 1);
        assert(game.submitAction(PlayCardAction {source.id(), "b43-terminal-borrow", {holder.id(), lord.id()}}).accepted);
        passNullificationChain(game);
        auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {holder.id(), request.requestId, CardId("b43-terminal-kill")}).accepted);
        request = *game.pendingResponse(); assert(request.type == ResponseType::Dodge && request.responder == lord.id()); assert(game.submitAction(RespondAction {lord.id(), request.requestId, std::nullopt}).accepted);
        declineAllRescues(game);
        assert(lord.status() == PlayerStatus::Dead && game.gameOver() && game.winningSide() == WinningSide::RebelSide);
        assert(!game.borrowedSwordContext() && !game.pendingResponse() && game.currentPlayer()->id() == source.id());
    }
    { // B4.4 core: DrawTwo is suspended for an unbounded Nullification chain and resumes by parity.
        const auto runChain = [](int nullifications) {
            auto game = fourPlayerGame(); auto& source = *game.players()[0]; auto& responder = *game.players()[1];
            source.addCard(card("b44-draw-" + std::to_string(nullifications), CardType::ExNihilo));
            for (int i = 0; i < nullifications; ++i) responder.addCard(card("b44-null-" + std::to_string(nullifications) + "-" + std::to_string(i), CardType::Nullification));
            const auto sourceBefore = source.handCards().size(); const auto discardBefore = game.deck().discardPileSize();
            assert(game.submitAction(PlayCardAction {source.id(), "b44-draw-" + std::to_string(nullifications), {}}).accepted);
            std::vector<PlayerId> order; std::uint64_t previousRequest = 0; int played = 0;
            while (game.pendingResponse()) {
                const auto request = *game.pendingResponse(); assert(request.type == ResponseType::Nullification && request.requestId != previousRequest); previousRequest = request.requestId; order.push_back(request.responder);
                std::optional<CardId> response;
                if (request.responder == responder.id() && played < nullifications) response = "b44-null-" + std::to_string(nullifications) + "-" + std::to_string(played++);
                assert(game.submitAction(RespondAction {request.responder, request.requestId, response}).accepted);
            }
            assert(played == nullifications && !game.trickResolution() && !game.nullificationChain());
            assert(source.handCards().size() == sourceBefore + (nullifications % 2 == 0 ? 1 : -1));
            assert(game.deck().discardPileSize() == discardBefore + 1 + nullifications);
            if (nullifications > 0) assert(!order.empty() && order.front() == 2);
            if (nullifications == 0) assert(std::find(order.begin(), order.end(), 1) == order.end());
            else assert(std::find(order.begin(), order.end(), 1) != order.end());
        };
        runChain(0); runChain(1); runChain(2); runChain(3); runChain(4);
    }
    { // B4.4 authority: stale, wrong-responder and non-Nullification cards cannot advance the current window.
        auto game = fourPlayerGame(); auto& p1 = *game.players()[0]; auto& p2 = *game.players()[1]; auto& p3 = *game.players()[2];
        p1.addCard(card("b44-authority-draw", CardType::ExNihilo)); p2.addCard(card("b44-illegal-slash", CardType::Slash)); p3.addCard(card("b44-private-null", CardType::Nullification));
        assert(game.submitAction(PlayCardAction {1, "b44-authority-draw", {}}).accepted); auto first = *game.pendingResponse(); assert(first.responder == 2);
        assert(!game.submitAction(RespondAction {2, first.requestId, CardId("b44-illegal-slash")}).accepted);
        assert(game.pendingResponse() && game.pendingResponse()->requestId == first.requestId && game.pendingResponse()->responder == 2);
        assert(game.submitAction(RespondAction {2, first.requestId, std::nullopt}).accepted);
        first = *game.pendingResponse(); assert(first.responder == 3);
        assert(!game.submitAction(RespondAction {3, first.requestId, CardId("b44-illegal-slash")}).accepted);
        assert(game.pendingResponse() && game.pendingResponse()->requestId == first.requestId && game.pendingResponse()->responder == 3);
        assert(game.submitAction(RespondAction {3, first.requestId, std::nullopt}).accepted);
        assert(!game.submitAction(RespondAction {3, first.requestId, std::nullopt}).accepted);
        passNullificationChain(game);
        assert(!game.trickResolution() && !game.nullificationChain());
    }
    { // B4.5 placement: delayed tricks enter the judgment zone immediately; their Nullification window opens before judgment.
        const auto runPlacement = [](CardType type, const std::string& prefix) {
            auto game = fourPlayerGame(); auto& source = *game.players()[0]; auto& responder = *game.players()[1];
            source.addCard(card(prefix + "-trick", type));
            const auto handBefore = source.handCards().size(); const auto discardedBefore = game.deck().discardPileSize();
            assert(game.submitAction(PlayCardAction {source.id(), prefix + "-trick", {responder.id()}}).accepted);
            assert(source.handCards().size() == handBefore - 1 && !game.pendingResponse() && !game.trickResolution() && !game.nullificationChain());
            assert(responder.hasJudgmentCard(type));
            assert(game.deck().discardPileSize() == discardedBefore);
        };
        for (const auto type : {CardType::Indulgence, CardType::SupplyShortage, CardType::Lightning}) {
            const auto prefix = type == CardType::Indulgence ? "b45-indulgence" : type == CardType::SupplyShortage ? "b45-supply" : "b45-lightning";
            runPlacement(type, prefix);
        }
    }
    { // B4.6: elemental Slashes share Slash legality, Dodge, Wine and the single per-phase limit.
        auto fireGame = combatGame(); auto& source = *fireGame.players()[0]; auto& target = *fireGame.players()[1];
        source.addCard(card("b46-fire", CardType::FireSlash));
        assert(fireGame.submitAction(PlayCardAction {source.id(), "b46-fire", {target.id()}}).accepted);
        const auto fireRequest = *fireGame.pendingResponse(); assert(fireGame.submitAction(RespondAction {target.id(), fireRequest.requestId, std::nullopt}).accepted && target.hp() == 3);
        source.addCard(card("b46-thunder-blocked", CardType::ThunderSlash));
        assert(!fireGame.submitAction(PlayCardAction {source.id(), "b46-thunder-blocked", {target.id()}}).accepted);

        auto thunderGame = combatGame(); auto& thunderSource = *thunderGame.players()[0]; auto& thunderTarget = *thunderGame.players()[1];
        thunderSource.addCard(card("b46-wine", CardType::Wine)); thunderSource.addCard(card("b46-thunder", CardType::ThunderSlash)); thunderTarget.addCard(card("b46-dodge", CardType::Dodge));
        assert(thunderGame.submitAction(PlayCardAction {thunderSource.id(), "b46-wine", {}}).accepted);
        assert(thunderGame.submitAction(PlayCardAction {thunderSource.id(), "b46-thunder", {thunderTarget.id()}}).accepted);
        const auto thunderRequest = *thunderGame.pendingResponse(); assert(thunderGame.submitAction(RespondAction {thunderTarget.id(), thunderRequest.requestId, CardId("b46-dodge")}).accepted && thunderTarget.hp() == 4);
    }
    { // B4.7 Bagua: red judgment is a virtual Dodge; black judgment falls through to the normal Dodge request.
        auto red = combatGame(); auto& source = *red.players()[0]; auto& target = *red.players()[1];
        red.testingEquip(target.id(), std::make_shared<Card>("b47-bagua", "八卦阵", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0}));
        red.testingAddToDrawPile(std::make_shared<Card>("b47-bagua-red", "judge", CardType::Slash, Suit::Heart, 1)); source.addCard(card("b47-bagua-red-slash", CardType::Slash)); const auto redDiscard = red.deck().discardPileSize();
        assert(red.submitAction(PlayCardAction {source.id(), "b47-bagua-red-slash", {target.id()}}).accepted && !red.pendingResponse() && target.hp() == 4 && red.deck().discardPileSize() == redDiscard + 2 && red.latestJudgment() && red.latestJudgment()->succeeded);
        assert(std::any_of(red.logEntries().begin(), red.logEntries().end(), [](const auto& entry) { return entry.find("triggered [Bagua]") != std::string::npos; })
            && std::any_of(red.logEntries().begin(), red.logEntries().end(), [](const auto& entry) { return entry.find("[judge] Heart 1") != std::string::npos; })
            && std::any_of(red.logEntries().begin(), red.logEntries().end(), [](const auto& entry) { return entry.find("judged red: [Bagua] succeeded") != std::string::npos; }));
        auto black = combatGame(); auto& blackSource = *black.players()[0]; auto& blackTarget = *black.players()[1];
        black.testingEquip(blackTarget.id(), std::make_shared<Card>("b47-bagua-black", "八卦阵", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); black.testingAddToDrawPile(std::make_shared<Card>("b47-bagua-black-judge", "judge", CardType::Slash, Suit::Spade, 1)); blackTarget.addCard(card("b47-bagua-dodge", CardType::Dodge)); blackSource.addCard(card("b47-bagua-black-slash", CardType::FireSlash));
        assert(black.submitAction(PlayCardAction {blackSource.id(), "b47-bagua-black-slash", {blackTarget.id()}}).accepted);
        const auto failedLog = std::find_if(black.logEntries().begin(), black.logEntries().end(), [](const auto& entry) { return entry.find("judged black: [Bagua] failed") != std::string::npos; });
        const auto dodgeLog = std::find_if(black.logEntries().begin(), black.logEntries().end(), [](const auto& entry) { return entry.find("respond with [Dodge]") != std::string::npos; });
        assert(failedLog != black.logEntries().end() && dodgeLog != black.logEntries().end() && failedLog < dodgeLog);
        const auto request = *black.pendingResponse(); assert(black.submitAction(RespondAction {blackTarget.id(), request.requestId, "b47-bagua-dodge"}).accepted && blackTarget.hp() == 4 && !black.pendingResponse());
        auto noDodge = combatGame(); auto& noDodgeSource = *noDodge.players()[0]; auto& noDodgeTarget = *noDodge.players()[1]; noDodge.testingEquip(noDodgeTarget.id(), std::make_shared<Card>("b47-bagua-no-dodge", "八卦阵", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); noDodge.testingAddToDrawPile(std::make_shared<Card>("b47-bagua-black-no-dodge", "judge", CardType::Slash, Suit::Club, 1)); noDodgeSource.addCard(card("b47-bagua-thunder", CardType::ThunderSlash));
        assert(noDodge.submitAction(PlayCardAction {noDodgeSource.id(), "b47-bagua-thunder", {noDodgeTarget.id()}}).accepted); const auto noDodgeRequest = *noDodge.pendingResponse(); assert(noDodge.submitAction(RespondAction {noDodgeTarget.id(), noDodgeRequest.requestId, std::nullopt}).accepted && noDodgeTarget.hp() == 3);
        assert(std::any_of(noDodge.logEntries().begin(), noDodge.logEntries().end(), [](const auto& entry) { return entry.find("judged black: [Bagua] failed") != std::string::npos; }));
        auto removed = combatGame(); auto& removedSource = *removed.players()[0]; auto& removedTarget = *removed.players()[1]; removed.testingEquip(removedTarget.id(), std::make_shared<Card>("b47-bagua-old", "八卦阵", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); removed.testingEquip(removedTarget.id(), std::make_shared<Card>("b47-bagua-new", "other", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); removed.testingAddToDrawPile(std::make_shared<Card>("b47-bagua-should-stay", "judge", CardType::Slash, Suit::Heart, 1)); removedSource.addCard(card("b47-bagua-removed-slash", CardType::Slash));
        assert(removed.submitAction(PlayCardAction {removedSource.id(), "b47-bagua-removed-slash", {removedTarget.id()}}).accepted && removed.pendingResponse());
        auto unequipped = combatGame(); auto& unequippedSource = *unequipped.players()[0]; auto& unequippedTarget = *unequipped.players()[1]; unequipped.testingEquip(unequippedTarget.id(), std::make_shared<Card>("b47-bagua-remove", "八卦阵", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); unequipped.testingRemoveEquipment(unequippedTarget.id(), EquipmentSlot::Armor); unequipped.testingAddToDrawPile(std::make_shared<Card>("b47-bagua-unused-judge", "judge", CardType::Slash, Suit::Heart, 1)); unequippedSource.addCard(card("b47-bagua-unequipped-slash", CardType::Slash));
        assert(unequipped.submitAction(PlayCardAction {unequippedSource.id(), "b47-bagua-unequipped-slash", {unequippedTarget.id()}}).accepted && unequipped.pendingResponse() && !unequipped.latestJudgment());
    }
    { // B4.7 Renwang Shield blocks only black ordinary Slash through the shared armor hook.
        const auto run = [](CardType type, Suit suit, int expectedHp) { auto game = combatGame(); auto& source = *game.players()[0]; auto& target = *game.players()[1]; game.testingEquip(target.id(), std::make_shared<Card>("b47-renwang", "仁王盾", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); source.addCard(std::make_shared<Card>("b47-renwang-slash", "slash", type, suit, 1)); assert(game.submitAction(PlayCardAction {source.id(), "b47-renwang-slash", {target.id()}}).accepted); const auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {target.id(), request.requestId, std::nullopt}).accepted && target.hp() == expectedHp && !game.pendingResponse()); };
        run(CardType::Slash, Suit::Spade, 4); run(CardType::Slash, Suit::Heart, 3); run(CardType::FireSlash, Suit::Spade, 3); run(CardType::ThunderSlash, Suit::Club, 3);
        const auto disabled = [](bool ignore, bool remove, bool replace) { auto game = combatGame(); auto& source = *game.players()[0]; auto& target = *game.players()[1]; game.testingEquip(target.id(), std::make_shared<Card>("b47-renwang-disable", "仁王盾", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); if (remove) game.testingRemoveEquipment(target.id(), EquipmentSlot::Armor); if (replace) game.testingEquip(target.id(), std::make_shared<Card>("b47-renwang-replace", "other", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); game.testingSetIgnoreArmorForNextSlash(ignore); source.addCard(std::make_shared<Card>("b47-renwang-disabled-slash", "slash", CardType::Slash, Suit::Spade, 1)); assert(game.submitAction(PlayCardAction {source.id(), "b47-renwang-disabled-slash", {target.id()}}).accepted); const auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {target.id(), request.requestId, std::nullopt}).accepted && target.hp() == 3); };
        disabled(true, false, false); disabled(false, true, false); disabled(false, false, true);
    }
    { // B4.7 Qinggang Sword ignores armor only while it remains in the current Weapon slot.
        const auto loss = [](bool replace) {
            auto game = combatGame();
            auto& source = *game.players()[0];
            auto& target = *game.players()[1];
            game.testingEquip(source.id(), std::make_shared<Card>("b47-qinggang", "青釭剑", CardType::Weapon, Suit::Spade, 1,
                EquipmentData {EquipmentSlot::Weapon, 2}));
            game.testingEquip(target.id(), std::make_shared<Card>("b47-renwang-target", "仁王盾", CardType::Armor, Suit::Spade, 1,
                EquipmentData {EquipmentSlot::Armor, 0}));

            source.addCard(std::make_shared<Card>("b47-qinggang-first", "slash", CardType::Slash, Suit::Spade, 1));
            assert(game.submitAction(PlayCardAction {source.id(), "b47-qinggang-first", {target.id()}}).accepted);
            const auto firstRequest = *game.pendingResponse();
            assert(game.submitAction(RespondAction {target.id(), firstRequest.requestId, std::nullopt}).accepted);
            assert(target.hp() == 3 && !game.pendingResponse());

            advanceToPlayerOne(game);
            if (replace) {
                source.addCard(std::make_shared<Card>("b47-other-weapon", "other", CardType::Weapon, Suit::Spade, 1,
                    EquipmentData {EquipmentSlot::Weapon, 2}));
                assert(game.submitAction(PlayCardAction {source.id(), "b47-other-weapon", {}}).accepted);
                assert(source.equipment(EquipmentSlot::Weapon)->id() == "b47-other-weapon");
            } else {
                game.testingRemoveEquipment(source.id(), EquipmentSlot::Weapon);
                assert(!source.equipment(EquipmentSlot::Weapon));
            }

            source.addCard(std::make_shared<Card>("b47-qinggang-second", "slash", CardType::Slash, Suit::Spade, 1));
            const auto second = game.submitAction(PlayCardAction {source.id(), "b47-qinggang-second", {target.id()}});
            assert(second.accepted);
            // Renwang resolves after the normal Dodge window.  Its restored block proves
            // the prior Qinggang Slash did not leak ignoreArmor into this context.
            const auto secondRequest = *game.pendingResponse();
            assert(game.submitAction(RespondAction {target.id(), secondRequest.requestId, std::nullopt}).accepted);
            assert(target.hp() == 3 && !game.pendingResponse());
        };
        loss(false);
        loss(true);
    }
    { // B4.7 Vine Armor nullifies only ordinary Slash before a Dodge request is created.
        auto game = combatGame(); auto& source = *game.players()[0]; auto& target = *game.players()[1]; game.testingEquip(target.id(), std::make_shared<Card>("b47-vine", "藤甲", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); source.addCard(card("b47-vine-slash", CardType::Slash));
        assert(game.submitAction(PlayCardAction {source.id(), "b47-vine-slash", {target.id()}}).accepted && target.hp() == 4 && !game.pendingResponse());
        const auto elemental = [](CardType type, int expectedHp) { auto game = combatGame(); auto& source = *game.players()[0]; auto& target = *game.players()[1]; game.testingEquip(target.id(), std::make_shared<Card>("b47-vine-elemental", "藤甲", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); source.addCard(card("b47-vine-elemental-slash", type)); assert(game.submitAction(PlayCardAction {source.id(), "b47-vine-elemental-slash", {target.id()}}).accepted); const auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {target.id(), request.requestId, std::nullopt}).accepted && target.hp() == expectedHp); };
        elemental(CardType::FireSlash, 2); elemental(CardType::ThunderSlash, 3);
        const auto dynamic = [](bool replace) { auto game = combatGame(); auto& source = *game.players()[0]; auto& target = *game.players()[1]; game.testingEquip(target.id(), std::make_shared<Card>("b47-vine-equip", "藤甲", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); if (replace) game.testingEquip(target.id(), std::make_shared<Card>("b47-vine-replace", "other", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); else game.testingRemoveEquipment(target.id(), EquipmentSlot::Armor); source.addCard(card("b47-vine-after", CardType::Slash)); assert(game.submitAction(PlayCardAction {source.id(), "b47-vine-after", {target.id()}}).accepted && game.pendingResponse()); };
        dynamic(false); dynamic(true);
    }
    { // B4.7 regression: Vine Armor also skips both AOE response windows and is read fresh for Fire damage.
        auto game = combatGame(); auto& source = *game.players()[0]; auto& target = *game.players()[1];
        game.testingEquip(target.id(), std::make_shared<Card>("b47-vine-aoe", "藤甲", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0}));
        source.addCard(card("b47-vine-barbarian", CardType::BarbarianInvasion));
        assert(game.submitAction(PlayCardAction {source.id(), "b47-vine-barbarian", {}}).accepted);
        while (game.pendingResponse()) { const auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        assert(target.hp() == 4 && !game.multiTargetEffect());
        source.addCard(card("b47-vine-arrow", CardType::ArrowBarrage));
        assert(game.submitAction(PlayCardAction {source.id(), "b47-vine-arrow", {}}).accepted);
        while (game.pendingResponse()) { const auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        assert(target.hp() == 4 && !game.multiTargetEffect());

        const auto fireAfterLoss = [](bool replace) {
            auto lossGame = combatGame(); auto& lossSource = *lossGame.players()[0]; auto& lossTarget = *lossGame.players()[1];
            lossGame.testingEquip(lossTarget.id(), std::make_shared<Card>("b47-vine-dynamic", "藤甲", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0}));
            if (replace) lossGame.testingEquip(lossTarget.id(), std::make_shared<Card>("b47-vine-other", "other", CardType::Armor, Suit::Club, 2, EquipmentData {EquipmentSlot::Armor, 0}));
            else lossGame.testingRemoveEquipment(lossTarget.id(), EquipmentSlot::Armor);
            lossSource.addCard(card("b47-vine-fire-after", CardType::FireSlash));
            assert(lossGame.submitAction(PlayCardAction {lossSource.id(), "b47-vine-fire-after", {lossTarget.id()}}).accepted);
            const auto request = *lossGame.pendingResponse();
            assert(lossGame.submitAction(RespondAction {lossTarget.id(), request.requestId, std::nullopt}).accepted && lossTarget.hp() == 3);
        };
        fireAfterLoss(false); fireAfterLoss(true);
    }
    { // B4.7 Qinggang regression: every Slash nature ignores the current target armor and does not inherit after loss.
        const auto elemental = [](CardType slashType) {
            auto game = combatGame(); auto& source = *game.players()[0]; auto& target = *game.players()[1];
            game.testingEquip(source.id(), std::make_shared<Card>("b47-qinggang-elemental", "青釭剑", CardType::Weapon, Suit::Spade, 1, EquipmentData {EquipmentSlot::Weapon, 2}));
            game.testingEquip(target.id(), std::make_shared<Card>("b47-qinggang-vine", "藤甲", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0}));
            source.addCard(card("b47-qinggang-elemental-slash", slashType));
            assert(game.submitAction(PlayCardAction {source.id(), "b47-qinggang-elemental-slash", {target.id()}}).accepted);
            const auto request = *game.pendingResponse();
            assert(game.submitAction(RespondAction {target.id(), request.requestId, std::nullopt}).accepted && target.hp() == 3);
        };
        elemental(CardType::FireSlash); elemental(CardType::ThunderSlash);
        auto baguaGame = combatGame(); auto& baguaSource = *baguaGame.players()[0]; auto& baguaTarget = *baguaGame.players()[1];
        baguaGame.testingEquip(baguaSource.id(), std::make_shared<Card>("b47-qinggang-bagua-weapon", "青釭剑", CardType::Weapon, Suit::Spade, 1, EquipmentData {EquipmentSlot::Weapon, 2}));
        baguaGame.testingEquip(baguaTarget.id(), std::make_shared<Card>("b47-qinggang-bagua", "八卦阵", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0}));
        baguaGame.testingAddToDrawPile(std::make_shared<Card>("b47-qinggang-bagua-red", "judge", CardType::Slash, Suit::Heart, 1)); baguaSource.addCard(card("b47-qinggang-bagua-slash", CardType::Slash));
        assert(baguaGame.submitAction(PlayCardAction {baguaSource.id(), "b47-qinggang-bagua-slash", {baguaTarget.id()}}).accepted && baguaGame.pendingResponse() && !baguaGame.latestJudgment());
    }
    { // B4.7 first equipment batch: Ancient Blade adds exactly one damage only to an empty-handed Slash target.
        auto emptyGame = combatGame(); auto& source = *emptyGame.players()[0]; auto& target = *emptyGame.players()[1];
        emptyGame.testingEquip(source.id(), std::make_shared<Card>("b47-ancient", "古锭刀", CardType::Weapon, Suit::Spade, 1, EquipmentData {EquipmentSlot::Weapon, 2})); source.addCard(card("b47-ancient-slash", CardType::Slash));
        assert(emptyGame.submitAction(PlayCardAction {source.id(), "b47-ancient-slash", {target.id()}}).accepted); auto request = *emptyGame.pendingResponse(); assert(emptyGame.submitAction(RespondAction {target.id(), request.requestId, std::nullopt}).accepted && target.hp() == 2);
        auto handGame = combatGame(); auto& handSource = *handGame.players()[0]; auto& handTarget = *handGame.players()[1]; handGame.testingEquip(handSource.id(), equipmentCard("b47-ancient-hand", CardType::Weapon, 2)); handTarget.addCard(card("b47-target-hand", CardType::Peach)); handSource.addCard(card("b47-ancient-no-bonus", CardType::FireSlash));
        assert(handGame.submitAction(PlayCardAction {handSource.id(), "b47-ancient-no-bonus", {handTarget.id()}}).accepted); request = *handGame.pendingResponse(); assert(handGame.submitAction(RespondAction {handTarget.id(), request.requestId, std::nullopt}).accepted && handTarget.hp() == 3);
        auto replacementGame = combatGame(); auto& replacementSource = *replacementGame.players()[0]; auto& replacementTarget = *replacementGame.players()[1]; replacementGame.testingEquip(replacementSource.id(), equipmentCard("b47-ancient-old", CardType::Weapon, 2)); replacementGame.testingEquip(replacementSource.id(), equipmentCard("b47-ordinary", CardType::Weapon, 2)); replacementSource.addCard(card("b47-ancient-removed", CardType::ThunderSlash));
        assert(replacementGame.submitAction(PlayCardAction {replacementSource.id(), "b47-ancient-removed", {replacementTarget.id()}}).accepted); request = *replacementGame.pendingResponse(); assert(replacementGame.submitAction(RespondAction {replacementTarget.id(), request.requestId, std::nullopt}).accepted && replacementTarget.hp() == 3);
    }
    { // B4.7 first equipment batch: Vermilion Fan turns only ordinary Slash into Fire and loses effect when replaced.
        auto fanGame = fourPlayerGame(); auto& source = *fanGame.players()[0]; auto& target = *fanGame.players()[1]; auto& chained = *fanGame.players()[2]; fanGame.testingEquip(source.id(), std::make_shared<Card>("b47-fan", "朱雀羽扇", CardType::Weapon, Suit::Spade, 1, EquipmentData {EquipmentSlot::Weapon, 4})); fanGame.testingSetChained(target.id(), true); fanGame.testingSetChained(chained.id(), true); source.addCard(card("b47-fan-slash", CardType::Slash));
        assert(fanGame.submitAction(PlayCardAction {source.id(), "b47-fan-slash", {target.id()}}).accepted); auto request = *fanGame.pendingResponse(); assert(fanGame.submitAction(RespondAction {target.id(), request.requestId, std::nullopt}).accepted && target.hp() == 3 && chained.hp() == 3);
        auto replacedGame = combatGame(); auto& replacedSource = *replacedGame.players()[0]; auto& replacedTarget = *replacedGame.players()[1]; replacedGame.testingEquip(replacedSource.id(), equipmentCard("b47-fan-old", CardType::Weapon, 4)); replacedGame.testingEquip(replacedSource.id(), equipmentCard("b47-fan-replacement", CardType::Weapon, 2)); replacedSource.addCard(card("b47-fan-removed", CardType::Slash));
        assert(replacedGame.submitAction(PlayCardAction {replacedSource.id(), "b47-fan-removed", {replacedTarget.id()}}).accepted); request = *replacedGame.pendingResponse(); assert(replacedGame.submitAction(RespondAction {replacedTarget.id(), request.requestId, std::nullopt}).accepted && replacedTarget.hp() == 3);
    }
    { // B4.6: Iron Chain toggles one or two distinct living targets, including its source, through Nullification.
        const auto resolve = [](GameEngine& game) {
            while (game.pendingResponse()) { const auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        };
        auto game = fourPlayerGame(); auto& p1 = *game.players()[0]; auto& p2 = *game.players()[1]; auto& p3 = *game.players()[2];
        p1.addCard(card("b46-chain-one", CardType::IronChain));
        assert(game.submitAction(PlayCardAction {p1.id(), "b46-chain-one", {p1.id()}}).accepted); resolve(game);
        assert(p1.isChained() && !game.trickResolution() && !game.nullificationChain() && !game.pendingResponse());
        p1.addCard(card("b46-chain-two", CardType::IronChain));
        assert(game.submitAction(PlayCardAction {p1.id(), "b46-chain-two", {p1.id(), p2.id()}}).accepted); resolve(game);
        assert(!p1.isChained() && p2.isChained());
        p1.addCard(card("b46-chain-duplicate", CardType::IronChain));
        assert(!game.submitAction(PlayCardAction {p1.id(), "b46-chain-duplicate", {p3.id(), p3.id()}}).accepted);
    }
    { // B4.6: one Nullification cancels Iron Chain; two restore it and all used cards are discarded.
        const auto run = [](int nullifications) {
            auto game = fourPlayerGame(); auto& p1 = *game.players()[0]; auto& p2 = *game.players()[1];
            p1.addCard(card("b46-chain-" + std::to_string(nullifications), CardType::IronChain));
            for (int i = 0; i < nullifications; ++i) p2.addCard(card("b46-chain-null-" + std::to_string(i), CardType::Nullification));
            const auto discardBefore = game.deck().discardPileSize();
            assert(game.submitAction(PlayCardAction {p1.id(), "b46-chain-" + std::to_string(nullifications), {p2.id()}}).accepted);
            int played = 0;
            while (game.pendingResponse()) { const auto request = *game.pendingResponse(); std::optional<CardId> response; if (request.responder == p2.id() && played < nullifications) response = "b46-chain-null-" + std::to_string(played++); assert(game.submitAction(RespondAction {request.responder, request.requestId, response}).accepted); }
            assert(p2.isChained() == (nullifications % 2 == 0));
            assert(game.deck().discardPileSize() == discardBefore + 1 + nullifications && !game.trickResolution() && !game.nullificationChain() && !game.pendingResponse());
        };
        run(0); run(1); run(2);
    }
    { // B4.6: Iron Chain recast discards itself and draws exactly one without creating a response context.
        auto game = fourPlayerGame(); auto& p1 = *game.players()[0];
        p1.addCard(card("b46-chain-recast", CardType::IronChain));
        const auto handBefore = p1.handCards().size(); const auto deckBefore = game.deck().drawPileSize(); const auto discardBefore = game.deck().discardPileSize();
        assert(game.submitAction(PlayCardAction {p1.id(), "b46-chain-recast", {}}).accepted);
        assert(p1.handCards().size() == handBefore && game.deck().drawPileSize() == deckBefore - 1 && game.deck().discardPileSize() == discardBefore + 1);
        assert(!game.pendingResponse() && !game.trickResolution() && !game.nullificationChain());
    }
    { // B4.6: elemental damage propagates in circular seat order, keeps amount/nature, resets chain state, and never propagates Normal damage.
        auto game = fourPlayerGame(); auto& p1 = *game.players()[0]; auto& p2 = *game.players()[1]; auto& p3 = *game.players()[2]; auto& p4 = *game.players()[3];
        game.testingSetChained(p2.id(), true); game.testingSetChained(p3.id(), true); game.testingSetChained(p4.id(), true);
        p1.addCard(card("b46-fire-propagation", CardType::FireSlash));
        assert(game.submitAction(PlayCardAction {p1.id(), "b46-fire-propagation", {p2.id()}}).accepted);
        const auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {p2.id(), request.requestId, std::nullopt}).accepted);
        assert(p2.hp() == 3 && p3.hp() == 3 && p4.hp() == 3 && !p2.isChained() && !p3.isChained() && !p4.isChained() && !game.chainDamageContext());

        auto normalGame = fourPlayerGame(); auto& n1 = *normalGame.players()[0]; auto& n2 = *normalGame.players()[1]; auto& n3 = *normalGame.players()[2];
        normalGame.testingSetChained(n2.id(), true); normalGame.testingSetChained(n3.id(), true); n1.addCard(card("b46-normal-no-chain", CardType::Slash));
        assert(normalGame.submitAction(PlayCardAction {n1.id(), "b46-normal-no-chain", {n2.id()}}).accepted);
        const auto normalRequest = *normalGame.pendingResponse(); assert(normalGame.submitAction(RespondAction {n2.id(), normalRequest.requestId, std::nullopt}).accepted);
        assert(n2.hp() == 3 && n3.hp() == 4 && n2.isChained() && n3.isChained() && !normalGame.chainDamageContext());
    }
    { // B4.6: Wine FireSlash keeps two Fire damage for every chained target; Thunder keeps its nature through propagation.
        auto wineGame = fourPlayerGame(); auto& p1 = *wineGame.players()[0]; auto& p2 = *wineGame.players()[1]; auto& p3 = *wineGame.players()[2];
        wineGame.testingSetChained(p2.id(), true); wineGame.testingSetChained(p3.id(), true); p1.addCard(card("b46-wine-chain", CardType::Wine)); p1.addCard(card("b46-fire-two", CardType::FireSlash));
        assert(wineGame.submitAction(PlayCardAction {p1.id(), "b46-wine-chain", {}}).accepted);
        assert(wineGame.submitAction(PlayCardAction {p1.id(), "b46-fire-two", {p2.id()}}).accepted);
        const auto fireRequest = *wineGame.pendingResponse(); assert(wineGame.submitAction(RespondAction {p2.id(), fireRequest.requestId, std::nullopt}).accepted);
        assert(p2.hp() == 2 && p3.hp() == 2 && !wineGame.chainDamageContext());

        auto thunderGame = fourPlayerGame(); auto& t1 = *thunderGame.players()[0]; auto& t2 = *thunderGame.players()[1]; auto& t3 = *thunderGame.players()[2];
        thunderGame.testingSetChained(t2.id(), true); thunderGame.testingSetChained(t3.id(), true); t1.addCard(card("b46-thunder-propagation", CardType::ThunderSlash));
        assert(thunderGame.submitAction(PlayCardAction {t1.id(), "b46-thunder-propagation", {t2.id()}}).accepted);
        const auto thunderRequest = *thunderGame.pendingResponse(); assert(thunderGame.submitAction(RespondAction {t2.id(), thunderRequest.requestId, std::nullopt}).accepted);
        assert(t2.hp() == 3 && t3.hp() == 3 && !thunderGame.chainDamageContext());
    }
    { // B4.6: propagated damage uses Dying/death flow and cleans chain context without stopping later legal targets.
        auto game = fourPlayerGame(); auto& p1 = *game.players()[0]; auto& p2 = *game.players()[1]; auto& p3 = *game.players()[2]; auto& p4 = *game.players()[3];
        game.testingSetHp(p3.id(), 1); game.testingSetChained(p2.id(), true); game.testingSetChained(p3.id(), true); game.testingSetChained(p4.id(), true); p1.addCard(card("b46-death-propagation", CardType::FireSlash));
        assert(game.submitAction(PlayCardAction {p1.id(), "b46-death-propagation", {p2.id()}}).accepted);
        auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {p2.id(), request.requestId, std::nullopt}).accepted);
        while (game.pendingResponse()) { request = *game.pendingResponse(); assert(game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        assert(p3.status() == PlayerStatus::Dead && p4.hp() == 3 && !game.chainDamageContext());
    }
    { // B4.6: a propagated Fire Dying can be rescued, and propagated Thunder can still complete death/GameOver.
        auto rescueGame = fourPlayerGame(); auto& p1 = *rescueGame.players()[0]; auto& p2 = *rescueGame.players()[1]; auto& p3 = *rescueGame.players()[2];
        rescueGame.testingSetHp(p3.id(), 1); rescueGame.testingSetChained(p2.id(), true); rescueGame.testingSetChained(p3.id(), true);
        p3.addCard(card("b46-propagated-peach", CardType::Peach)); p1.addCard(card("b46-propagated-fire", CardType::FireSlash));
        assert(rescueGame.submitAction(PlayCardAction {p1.id(), "b46-propagated-fire", {p2.id()}}).accepted);
        auto request = *rescueGame.pendingResponse(); assert(rescueGame.submitAction(RespondAction {p2.id(), request.requestId, std::nullopt}).accepted);
        while (rescueGame.pendingResponse()) { request = *rescueGame.pendingResponse(); const std::optional<CardId> response = request.responder == p3.id() ? std::optional<CardId> {"b46-propagated-peach"} : std::nullopt; assert(rescueGame.submitAction(RespondAction {request.responder, request.requestId, response}).accepted); }
        assert(p3.status() == PlayerStatus::Alive && p3.hp() == 1 && !rescueGame.chainDamageContext());

        auto deathGame = combatGame(); auto& d1 = *deathGame.players()[0]; auto& d2 = *deathGame.players()[1];
        deathGame.testingSetHp(d2.id(), 1); deathGame.testingSetChained(d2.id(), true); d1.addCard(card("b46-thunder-death", CardType::ThunderSlash));
        assert(deathGame.submitAction(PlayCardAction {d1.id(), "b46-thunder-death", {d2.id()}}).accepted);
        request = *deathGame.pendingResponse(); assert(deathGame.submitAction(RespondAction {d2.id(), request.requestId, std::nullopt}).accepted);
        while (deathGame.pendingResponse()) { request = *deathGame.pendingResponse(); assert(deathGame.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        assert(d2.status() == PlayerStatus::Dead && deathGame.gameOver() && !deathGame.chainDamageContext());
    }
    { // B4.6: Fire Attack rejects an empty-hand target and its 0/1/2 Nullification chain has the normal parity.
        auto rejected = fourPlayerGame(); auto& source = *rejected.players()[0]; auto& target = *rejected.players()[1];
        source.addCard(card("b46-fire-attack-empty", CardType::FireAttack));
        assert(!rejected.submitAction(PlayCardAction {source.id(), "b46-fire-attack-empty", {target.id()}}).accepted);
        const auto run = [](int nullifications) {
            auto game = fourPlayerGame(); auto& p1 = *game.players()[0]; auto& p2 = *game.players()[1];
            p1.addCard(card("b46-fire-attack-" + std::to_string(nullifications), CardType::FireAttack)); p1.addCard(card("b46-fire-match-" + std::to_string(nullifications), CardType::Slash)); p2.addCard(card("b46-fire-reveal-" + std::to_string(nullifications), CardType::Dodge));
            for (int i = 0; i < nullifications; ++i) p2.addCard(card("b46-fire-null-" + std::to_string(i), CardType::Nullification));
            const auto discardBefore = game.deck().discardPileSize();
            assert(game.submitAction(PlayCardAction {p1.id(), "b46-fire-attack-" + std::to_string(nullifications), {p2.id()}}).accepted);
            int played = 0;
            while (game.pendingResponse()) { const auto request = *game.pendingResponse(); std::optional<CardId> response; if (request.responder == p2.id() && played < nullifications) response = "b46-fire-null-" + std::to_string(played++); assert(game.submitAction(RespondAction {request.responder, request.requestId, response}).accepted); }
            if (nullifications % 2) { assert(!game.pendingCardSelection() && !game.fireAttackContext() && p2.hp() == 4 && game.deck().discardPileSize() == discardBefore + 1 + nullifications); return; }
            const auto reveal = *game.pendingCardSelection(); assert(reveal.purpose == CardSelectionPurpose::FireAttackReveal && reveal.requester == p2.id());
            assert(game.submitAction(SelectCardsAction {p2.id(), reveal.requestId, {"b46-fire-reveal-" + std::to_string(nullifications)}}).accepted);
            const auto discard = *game.pendingCardSelection(); assert(discard.purpose == CardSelectionPurpose::FireAttackDiscard && discard.requester == p1.id());
            assert(game.submitAction(SelectCardsAction {p1.id(), discard.requestId, {"b46-fire-match-" + std::to_string(nullifications)}}).accepted);
            assert(p2.hp() == 3 && !game.fireAttackContext() && !game.pendingCardSelection() && game.deck().discardPileSize() == discardBefore + 2 + nullifications);
        };
        run(0); run(1); run(2);
    }
    { // B4.6: Fire Attack reveal stays in the target hand, requests are fresh/stale-safe, and its Fire damage reuses chain propagation.
        auto game = fourPlayerGame(); auto& p1 = *game.players()[0]; auto& p2 = *game.players()[1]; auto& p3 = *game.players()[2];
        game.testingSetChained(p2.id(), true); game.testingSetChained(p3.id(), true);
        p1.addCard(card("b46-fire-attack-chain", CardType::FireAttack)); p1.addCard(card("b46-fire-attack-suit", CardType::Slash)); p2.addCard(card("b46-fire-attack-visible", CardType::Dodge));
        assert(game.submitAction(PlayCardAction {p1.id(), "b46-fire-attack-chain", {p2.id()}}).accepted);
        while (game.pendingResponse()) { const auto response = *game.pendingResponse(); assert(game.submitAction(RespondAction {response.responder, response.requestId, std::nullopt}).accepted); }
        const auto reveal = *game.pendingCardSelection(); const auto handBefore = p2.handCards().size();
        assert(!game.submitAction(SelectCardsAction {p2.id(), reveal.requestId + 1, {"b46-fire-attack-visible"}}).accepted);
        assert(game.submitAction(SelectCardsAction {p2.id(), reveal.requestId, {"b46-fire-attack-visible"}}).accepted && p2.handCards().size() == handBefore);
        const auto discard = *game.pendingCardSelection(); assert(discard.requestId != reveal.requestId);
        assert(!game.submitAction(SelectCardsAction {p1.id(), reveal.requestId, {"b46-fire-attack-suit"}}).accepted);
        assert(game.submitAction(SelectCardsAction {p1.id(), discard.requestId, {"b46-fire-attack-suit"}}).accepted);
        assert(p2.hp() == 3 && p3.hp() == 3 && !p2.isChained() && !p3.isChained() && !game.chainDamageContext() && !game.fireAttackContext());
    }
    { // B4.6: Fire Attack pass or no matching suit does not deal damage and cleans both interaction contexts.
        const auto runPass = [](CardType sourceCard) {
            auto game = fourPlayerGame(); auto& p1 = *game.players()[0]; auto& p2 = *game.players()[1];
            p1.addCard(card("b46-fire-attack-pass", CardType::FireAttack)); p1.addCard(std::make_shared<Card>("b46-fire-wrong-suit", "wrong", sourceCard, Suit::Heart, 1)); p2.addCard(card("b46-fire-pass-reveal", CardType::Dodge));
            assert(game.submitAction(PlayCardAction {p1.id(), "b46-fire-attack-pass", {p2.id()}}).accepted);
            while (game.pendingResponse()) { const auto response = *game.pendingResponse(); assert(game.submitAction(RespondAction {response.responder, response.requestId, std::nullopt}).accepted); }
            const auto reveal = *game.pendingCardSelection(); assert(game.submitAction(SelectCardsAction {p2.id(), reveal.requestId, {"b46-fire-pass-reveal"}}).accepted);
            const auto discard = *game.pendingCardSelection(); assert(discard.selectableCards.empty());
            assert(game.submitAction(SelectCardsAction {p1.id(), discard.requestId, {}}).accepted && p2.hp() == 4 && !game.fireAttackContext() && !game.pendingCardSelection());
        };
        runPass(CardType::Slash);
    }
    { // B4.5 judgment: an empty judgment zone consumes no extra card and cannot create an interaction.
        auto game = combatGame(); auto& target = *game.players()[1];
        const auto drawBefore = game.deck().drawPileSize();
        finishCurrentTurn(game);
        assert(game.currentPlayer()->id() == target.id() && game.currentPhase() == Phase::Play);
        assert(game.deck().drawPileSize() == drawBefore - 2 && target.judgmentCards().empty());
        assert(!game.judgmentContext() && !game.latestJudgment() && !game.pendingResponse() && !game.pendingCardSelection());
    }
    { // B4.5 placement: duplicate delayed tricks are rejected while different types coexist in insertion order.
        auto game = fourPlayerGame(); auto& source = *game.players()[0]; auto& target = *game.players()[1];
        const auto place = [&](CardType type, const std::string& id) {
            source.addCard(card(id, type)); assert(game.submitAction(PlayCardAction {source.id(), id, {target.id()}}).accepted);
            while (game.pendingResponse()) { const auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        };
        place(CardType::Indulgence, "b45-duplicate-first");
        source.addCard(card("b45-duplicate-second", CardType::Indulgence));
        assert(!game.submitAction(PlayCardAction {source.id(), "b45-duplicate-second", {target.id()}}).accepted);
        place(CardType::SupplyShortage, "b45-coexist-supply"); place(CardType::Lightning, "b45-coexist-lightning");
        assert(target.judgmentCards().size() == 3 && target.judgmentCards()[0]->type() == CardType::Indulgence
            && target.judgmentCards()[1]->type() == CardType::SupplyShortage && target.judgmentCards()[2]->type() == CardType::Lightning);
    }
    { // B4.5 judgment: empty zones need no request, and Indulgence Heart consumes exactly one judgment card without skipping Play.
        auto game = combatGame(); auto& source = *game.players()[0]; auto& target = *game.players()[1];
        source.addCard(card("b45-heart-indulgence", CardType::Indulgence));
        assert(game.submitAction(PlayCardAction {source.id(), "b45-heart-indulgence", {target.id()}}).accepted);
        while (game.pendingResponse()) { const auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        game.testingAddToDrawPile(std::make_shared<Card>("b45-heart-judge", "heart", CardType::Slash, Suit::Heart, 7));
        const auto drawBefore = game.deck().drawPileSize(); const auto discardBefore = game.deck().discardPileSize();
        finishCurrentTurn(game);
        passNullificationChain(game);
        assert(game.currentPlayer()->id() == target.id() && game.currentPhase() == Phase::Play && target.judgmentCards().empty());
        assert(game.deck().drawPileSize() == drawBefore - 3 && game.deck().discardPileSize() == discardBefore + 2);
        assert(std::any_of(game.logEntries().begin(), game.logEntries().end(), [](const auto& entry) { return entry.find("judged [heart] Heart 7") != std::string::npos; }));
        assert(!game.judgmentContext() && !game.pendingResponse());
    }
    { // B4.5 judgment: a non-Heart Indulgence skips only this turn's Play and is fully discarded.
        auto game = combatGame(); auto& source = *game.players()[0]; auto& target = *game.players()[1];
        target.addCard(card("b45-skip-hand-1", CardType::Slash)); target.addCard(card("b45-skip-hand-2", CardType::Slash)); target.addCard(card("b45-skip-hand-3", CardType::Slash));
        source.addCard(card("b45-club-indulgence", CardType::Indulgence));
        assert(game.submitAction(PlayCardAction {source.id(), "b45-club-indulgence", {target.id()}}).accepted);
        while (game.pendingResponse()) { const auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        game.testingAddToDrawPile(std::make_shared<Card>("b45-club-judge", "club", CardType::Slash, Suit::Club, 7));
        finishCurrentTurn(game);
        passNullificationChain(game);
        assert(game.currentPlayer()->id() == target.id() && game.currentPhase() == Phase::Discard && target.judgmentCards().empty()
            && game.requiredDiscardCount() == 1);
        assert(game.submitAction(DiscardAction {target.id(), {target.handCards().front()->id()}}).accepted);
        assert(game.currentPlayer()->id() == source.id() && game.currentPhase() == Phase::Play && target.judgmentCards().empty());
        assert(std::any_of(game.logEntries().begin(), game.logEntries().end(), [](const auto& entry) { return entry.find("skipped Play phase") != std::string::npos; }));
        assert(!game.judgmentContext() && !game.pendingResponse());
        finishCurrentTurn(game);
        assert(game.currentPlayer()->id() == target.id() && game.currentPhase() == Phase::Play);
    }
    { // B4.5 judgment: Supply Shortage and Lightning share deterministic judgment execution and cleanup.
        auto game = combatGame(); auto& source = *game.players()[0]; auto& target = *game.players()[1];
        const auto place = [&](CardType type, const std::string& id) {
            source.addCard(card(id, type)); assert(game.submitAction(PlayCardAction {source.id(), id, {target.id()}}).accepted);
            while (game.pendingResponse()) { const auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        };
        place(CardType::Indulgence, "b45-order-indulgence"); place(CardType::SupplyShortage, "b45-order-supply");
        game.testingAddToDrawPile(std::make_shared<Card>("b45-order-club", "club", CardType::Slash, Suit::Club, 7));
        game.testingAddToDrawPile(std::make_shared<Card>("b45-order-heart", "heart", CardType::Slash, Suit::Heart, 7));
        finishCurrentTurn(game);
        passNullificationChain(game);
        assert(game.currentPlayer()->id() == target.id() && game.currentPhase() == Phase::Play && target.judgmentCards().empty() && !game.judgmentContext());

        finishCurrentTurn(game); // Source's next turn.
        source.addCard(card("b45-lightning", CardType::Lightning));
        assert(game.submitAction(PlayCardAction {source.id(), "b45-lightning", {target.id()}}).accepted);
        while (game.pendingResponse()) { const auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        game.testingAddToDrawPile(std::make_shared<Card>("b45-lightning-hit", "spade", CardType::Slash, Suit::Spade, 7));
        const auto hpBefore = target.hp(); finishCurrentTurn(game); passNullificationChain(game);
        assert(target.hp() == hpBefore - 3 && target.judgmentCards().empty() && !game.judgmentContext());
        assert(std::any_of(game.logEntries().begin(), game.logEntries().end(), [](const auto& entry) { return entry.find("judged [spade] Spade 7") != std::string::npos; }));
    }
    { // B4.5 judgment: failed Supply Shortage skips Draw only, and missed Lightning preserves identity on transfer.
        auto supplyGame = combatGame(); auto& source = *supplyGame.players()[0]; auto& target = *supplyGame.players()[1];
        target.addCard(card("b45-supply-hand-1", CardType::Slash)); target.addCard(card("b45-supply-hand-2", CardType::Slash)); target.addCard(card("b45-supply-hand-3", CardType::Slash));
        source.addCard(card("b45-supply-fail", CardType::SupplyShortage));
        assert(supplyGame.submitAction(PlayCardAction {source.id(), "b45-supply-fail", {target.id()}}).accepted);
        while (supplyGame.pendingResponse()) { const auto request = *supplyGame.pendingResponse(); assert(supplyGame.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        supplyGame.testingAddToDrawPile(std::make_shared<Card>("b45-supply-spade", "spade", CardType::Slash, Suit::Spade, 7));
        const auto targetHandBefore = target.handCards().size(); finishCurrentTurn(supplyGame); passNullificationChain(supplyGame);
        assert(target.handCards().size() == targetHandBefore && std::any_of(supplyGame.logEntries().begin(), supplyGame.logEntries().end(), [](const auto& entry) { return entry.find("skipped Draw phase") != std::string::npos; }));
        assert(std::any_of(supplyGame.logEntries().begin(), supplyGame.logEntries().end(), [](const auto& entry) { return entry.find("judged [spade] Spade 7") != std::string::npos; }));
        finishCurrentTurn(supplyGame);
        assert(supplyGame.currentPlayer()->id() == source.id() && supplyGame.currentPhase() == Phase::Play);
        const auto recoveryBefore = target.handCards().size(); finishCurrentTurn(supplyGame);
        assert(supplyGame.currentPlayer()->id() == target.id() && supplyGame.currentPhase() == Phase::Play && target.handCards().size() == recoveryBefore + 2);

        auto lightningGame = combatGame(); auto& lightningSource = *lightningGame.players()[0]; auto& lightningTarget = *lightningGame.players()[1];
        lightningSource.addCard(card("b45-lightning-miss", CardType::Lightning));
        assert(lightningGame.submitAction(PlayCardAction {lightningSource.id(), "b45-lightning-miss", {lightningTarget.id()}}).accepted);
        while (lightningGame.pendingResponse()) { const auto request = *lightningGame.pendingResponse(); assert(lightningGame.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        lightningGame.testingAddToDrawPile(std::make_shared<Card>("b45-lightning-miss-judge", "heart", CardType::Slash, Suit::Heart, 7));
        finishCurrentTurn(lightningGame); passNullificationChain(lightningGame);
        assert(lightningTarget.judgmentCards().empty() && lightningSource.judgmentCards().size() == 1
            && lightningSource.judgmentCards().front()->id() == "b45-lightning-miss" && !lightningGame.judgmentContext());

        auto noTargetGame = combatGame(); auto& noTargetSource = *noTargetGame.players()[0]; auto& noTarget = *noTargetGame.players()[1];
        noTargetSource.addCard(card("b45-lightning-no-target", CardType::Lightning));
        assert(noTargetGame.submitAction(PlayCardAction {noTargetSource.id(), "b45-lightning-no-target", {noTarget.id()}}).accepted);
        while (noTargetGame.pendingResponse()) { const auto request = *noTargetGame.pendingResponse(); assert(noTargetGame.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); }
        noTargetGame.testingAddToDrawPile(std::make_shared<Card>("b45-lightning-no-target-judge", "heart", CardType::Slash, Suit::Heart, 7));
        noTargetGame.testingSetPlayerStatus(noTargetSource.id(), PlayerStatus::Dead);
        const auto discardBefore = noTargetGame.deck().discardPileSize(); finishCurrentTurn(noTargetGame); passNullificationChain(noTargetGame);
        assert(noTarget.judgmentCards().empty() && noTargetGame.deck().discardPileSize() == discardBefore + 2
            && !noTargetGame.judgmentContext() && !noTargetGame.latestJudgment()->succeeded);
    }
    { // Stage 3.2: Serpent Spear virtual Slash uses two real hand cards and the unified Slash response path.
        auto game = combatGame(); auto& source = *game.players()[0]; auto& target = *game.players()[1];
        game.testingEquip(source.id(), std::make_shared<Card>("spear", "丈八蛇矛", CardType::Weapon, Suit::Spade, 12, EquipmentData {EquipmentSlot::Weapon, 3}));
        source.addCard(card("spear-a", CardType::Peach)); source.addCard(card("spear-b", CardType::Dodge));
        const auto discardBefore = game.deck().discardPileSize();
        assert(game.submitAction(PlayVirtualSlashAction {source.id(), {"spear-a", "spear-b"}, {target.id()}}).accepted);
        assert(source.handCards().empty() && game.deck().discardPileSize() == discardBefore + 2 && game.pendingResponse());
        const auto dodge = *game.pendingResponse(); assert(game.submitAction(RespondAction {target.id(), dodge.requestId, std::nullopt}).accepted);
        assert(!game.pendingResponse() && game.deck().drawPileSize() <= 161);
        source.addCard(card("only-one", CardType::Peach));
        assert(!game.submitAction(PlayVirtualSlashAction {source.id(), {"only-one", "only-one"}, {target.id()}}).accepted);
        assert(!game.submitAction(PlayVirtualSlashAction {source.id(), {"only-one", "missing"}, {target.id()}}).accepted);
        game.testingRemoveEquipment(source.id(), EquipmentSlot::Weapon); source.addCard(card("spear-c", CardType::Peach));
        assert(!game.submitAction(PlayVirtualSlashAction {source.id(), {"only-one", "spear-c"}, {target.id()}}).accepted);
    }
    { // Serpent Spear responds to Duel through the same ResponseType::Slash authority path.
        auto game = combatGame(); auto& source = *game.players()[0]; auto& target = *game.players()[1];
        game.testingEquip(target.id(), std::make_shared<Card>("spear-response", "丈八蛇矛", CardType::Weapon, Suit::Spade, 12, EquipmentData {EquipmentSlot::Weapon, 3}));
        source.addCard(card("spear-duel", CardType::Duel)); target.addCard(card("spear-r1", CardType::Peach)); target.addCard(card("spear-r2", CardType::Dodge));
        assert(game.submitAction(PlayCardAction {source.id(), "spear-duel", {target.id()}}).accepted); passNullificationChain(game); const auto request = *game.pendingResponse();
        assert(!game.submitAction(RespondVirtualSlashAction {target.id(), request.requestId + 1, {"spear-r1", "spear-r2"}}).accepted);
        assert(game.submitAction(RespondVirtualSlashAction {target.id(), request.requestId, {"spear-r1", "spear-r2"}}).accepted);
        assert(target.handCards().empty() && game.pendingResponse() && game.pendingResponse()->responder == source.id());
    }
    runHalberdTests();
    std::cout << "All Stage 2 through B4.5 core tests passed.\n";
}
