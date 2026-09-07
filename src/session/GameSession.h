#pragma once

#include <unordered_map>
#include <functional>
#include <unordered_set>
#include <string>
#include <vector>

#include "core/GameAction.h"
#include "core/GameEngine.h"
#include "session/PlayerViewState.h"

namespace sanguosha {

class GameSession {
public:
    explicit GameSession(const std::vector<std::string>& playerNames, bool startImmediately = true,
                         const std::vector<PlayerIdentity>& identities = {},
                         const std::vector<PlayerControlType>& controlTypes = {}, GameMode mode = GameMode::FreeForAll);
    void startGame();
    void configureGame(const std::vector<std::string>& playerNames,
                       const std::vector<PlayerIdentity>& identities = {},
                       const std::vector<PlayerControlType>& controlTypes = {}, GameMode mode = GameMode::FreeForAll);

    SessionClientId addClient(PlayerId playerId);
    void disconnectClient(SessionClientId clientId);
    void setPlayerConnected(PlayerId playerId, bool connected);
    ActionResult handlePlayerDisconnect(PlayerId playerId, bool resolveInteractions = true);
    ActionResult submitAction(SessionClientId clientId, const GameAction& action);
    ActionResult submitCardSelection(SessionClientId clientId, std::uint64_t requestId, SelectionOptionId optionId);
    ActionResult submitCardSelection(SessionClientId clientId, std::uint64_t requestId, const std::vector<SelectionOptionId>& optionIds);
    [[nodiscard]] PlayerViewState viewFor(SessionClientId clientId) const;
    [[nodiscard]] bool canPlayCard(SessionClientId clientId, const CardId& cardId) const;
    [[nodiscard]] bool canTakeTurnAction(SessionClientId clientId) const;
    [[nodiscard]] bool canPlayCardOnTarget(SessionClientId clientId, const CardId& cardId, PlayerId targetId) const;
    [[nodiscard]] std::vector<std::pair<PlayerId, PlayerId>> borrowedSwordTargetPairs(SessionClientId clientId, const CardId& cardId) const;
    [[nodiscard]] int requiredDiscardCount(SessionClientId clientId) const;
    [[nodiscard]] std::vector<PlayerId> aiPlayerIds() const;
    void restart(SessionClientId clientId);
    ActionResult handleTimeout();
    void appendPublicLog(std::string entry);
    [[nodiscard]] std::string timeoutContextKey() const;
    void setTimeoutView(TimeoutKind kind, PlayerId playerId, int remainingMs);
    void updateTimeoutView(int remainingMs);
    void subscribeToGameEvents(EventDispatcher::Listener listener);
    // Runs after a submitted action has fully settled, including declines that
    // finish an interaction without emitting another engine event.
    void subscribeToActions(std::function<void()> listener);
#ifdef SANGUOSHA_TESTING
    GameEngine& testingEngine() noexcept;
    [[nodiscard]] int testingRejectedActionCount() const noexcept { return rejectedActionCount_; }
#endif

private:
    ActionResult recordActionResult(ActionResult result);
    [[nodiscard]] PlayerId playerFor(SessionClientId clientId) const;
    [[nodiscard]] PlayerViewState buildViewFor(PlayerId viewer) const;
    [[nodiscard]] static CardView makeCardView(const Card& card);
    [[nodiscard]] ActionResult unauthorized() const;

    GameEngine engine_;
    std::vector<std::function<void()>> actionListeners_;
#ifdef SANGUOSHA_TESTING
    int rejectedActionCount_ {0};
#endif
    SessionClientId nextClientId_ {1};
    std::unordered_map<SessionClientId, PlayerId> clientPlayers_;
    std::unordered_set<PlayerId> disconnectedPlayers_;
    TimeoutKind timeoutKind_ {TimeoutKind::None}; PlayerId timeoutPlayerId_ {0}; int timeoutRemainingMs_ {0};
};

} // namespace sanguosha
