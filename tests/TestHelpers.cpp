#ifdef NDEBUG
#undef NDEBUG
#endif

#include "TestHelpers.h"

#include <cassert>
#include <vector>

using namespace sanguosha;

std::shared_ptr<Card> card(const std::string& id, CardType type) { return std::make_shared<Card>(id, id, type, Suit::Spade, 1); }
std::shared_ptr<Card> equipmentCard(const std::string& id, CardType type, int range) { const auto slot = equipmentSlotForCard(type); assert(slot); return std::make_shared<Card>(id, id, type, Suit::Spade, 1, EquipmentData {*slot, range}); }
void clearHand(Player& player) { std::vector<CardId> ids; for (const auto& existing : player.handCards()) ids.push_back(existing->id()); for (const auto& id : ids) player.removeCard(id); }
GameEngine combatGame() { GameEngine game; game.createGame({"Player 1", "Player 2"}); game.startGame(); clearHand(*game.players()[0]); clearHand(*game.players()[1]); assert(game.currentPlayer()->id() == 1 && game.currentPhase() == Phase::Play); return game; }
GameEngine fourPlayerGame() { GameEngine game; game.createGame({"Player 1", "Player 2", "Player 3", "Player 4"}); game.startGame(); for (const auto& player : game.players()) clearHand(*player); assert(game.currentPlayer()->id() == 1 && game.currentPhase() == Phase::Play); return game; }
void finishCurrentTurn(GameEngine& game) { assert(game.submitAction(EndPlayPhaseAction {game.currentPlayer()->id()}).accepted); if (game.currentPhase() == Phase::Discard) { std::vector<CardId> ids; for (int i = 0; i < game.requiredDiscardCount(); ++i) ids.push_back(game.currentPlayer()->handCards().at(i)->id()); assert(game.submitAction(DiscardAction {game.currentPlayer()->id(), ids}).accepted); } }
