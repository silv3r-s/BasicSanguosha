#pragma once

#include <memory>
#include <string>

#include "cards/Card.h"
#include "core/GameEngine.h"

std::shared_ptr<sanguosha::Card> card(const std::string& id, sanguosha::CardType type);
std::shared_ptr<sanguosha::Card> equipmentCard(const std::string& id, sanguosha::CardType type, int range = 0);
void clearHand(sanguosha::Player& player);
sanguosha::GameEngine combatGame();
sanguosha::GameEngine fourPlayerGame();
void finishCurrentTurn(sanguosha::GameEngine& game);
