#pragma once

#include <functional>
#include <vector>

#include "core/GameEvent.h"

namespace sanguosha {

class EventDispatcher {
public:
    using Listener = std::function<void(const GameEvent &)>;
    void subscribe(Listener listener);
    void dispatch(const GameEvent &event) const;

private:
    std::vector<Listener> listeners_;
};

} // namespace sanguosha
