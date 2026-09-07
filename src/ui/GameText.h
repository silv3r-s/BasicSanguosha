#pragma once

#include <QString>
#include <cstdint>
#include <vector>

#include <string>

#include "cards/Card.h"
#include "core/GameState.h"
#include "core/Player.h"
#include "core/ResponseRequest.h"
#include "core/GameAction.h"
#include "session/PlayerViewState.h"

namespace sanguosha::ui {

QString phaseToDisplayName(Phase phase);
QString cardTypeToDisplayName(CardType type);
QString cardDisplayName(const Card &card);
QString equipmentSlotToDisplayName(EquipmentSlot slot);
QString cardCategoryDisplayName(CardType type);
QString cardDescription(const Card &card);
QString suitToDisplayName(Suit suit);
QString suitToSymbol(Suit suit);
QString rankToDisplayName(int rank);
QString playerDisplayName(const Player &player);
QString playerDisplayName(const std::string &internalName);
QString playerStatusToDisplayName(PlayerStatus status);
QString playerIdentityToDisplayName(PlayerIdentity identity);
QString winningSideToDisplayName(WinningSide side);
QString responseTypeToDisplayName(ResponseType type);
QString actionErrorToDisplayName(ActionResult::Error error);
QString logEntryToDisplay(const std::string &entry);
[[nodiscard]] bool showLogEntryInRecentEvent(const std::string& entry);
QString formatEquipmentEffect(const EquipmentEffectEventView& event, const QString& targetName);
std::vector<EquipmentEffectEventView> newEquipmentEffects(const std::vector<EquipmentEffectEventView>& effects, std::uint64_t& lastDisplayedEventId);

} // namespace sanguosha::ui
