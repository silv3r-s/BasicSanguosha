#include "core/EventDispatcher.h"

namespace sanguosha {

void EventDispatcher::subscribe(Listener listener) { listeners_.push_back(std::move(listener)); }
void EventDispatcher::dispatch(const GameEvent &event) const { for (const auto &listener : listeners_) listener(event); }

} // namespace sanguosha
