#pragma once

#include <functional>
#include <memory>
#include <string>

#include <QObject>
#include <QTimer>

#include "ai/AIActionDecider.h"
#include "ai/AIIdentityInference.h"
#include "session/GameSession.h"

namespace sanguosha::ai {

class AIController final : public QObject {
public:
    AIController(GameSession& session, PlayerId playerId, std::function<void()> actionApplied,
                 std::unique_ptr<IAIActionDecider> decider = std::make_unique<BasicAIActionDecider>(), QObject* parent = nullptr);

    void schedule();
    void stop();
    void reset();
#ifdef SANGUOSHA_TESTING
    void setPacingEnabledForTesting(bool enabled, int delayOverrideMs = -1) noexcept;
#endif
    [[nodiscard]] PlayerId playerId() const noexcept { return playerId_; }

private:
    void act();
    [[nodiscard]] std::string interactionKey(const PlayerViewState& view) const;

    GameSession& session_;
    PlayerId playerId_;
    SessionClientId clientId_;
    std::function<void()> actionApplied_;
    std::unique_ptr<IAIActionDecider> decider_;
    AIIdentityInference identityInference_;
    std::string lastInteractionKey_;
    bool scheduled_ {false};
    bool stopped_ {false};
    QTimer pacingTimer_;
    std::string scheduledInteractionKey_;
    int lastScheduledTurnNumber_ {0};
#ifdef SANGUOSHA_TESTING
    bool pacingEnabled_ {false};
    int testingDelayOverrideMs_ {-1};
#else
    bool pacingEnabled_ {true};
#endif
};

} // namespace sanguosha::ai
