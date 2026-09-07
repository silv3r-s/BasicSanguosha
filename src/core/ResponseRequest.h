#pragma once

#include <cstdint>

#include "cards/Card.h"
#include "core/Player.h"

namespace sanguosha {

enum class ResponseType { Dodge, Slash, PeachRescue, BorrowedSwordSlash, Nullification, QinglongSlash };

struct ResponseRequest {
    std::uint64_t requestId;
    ResponseType type;
    PlayerId requester;
    PlayerId responder;
    CardId sourceCardId;
    bool allowDecline {true};
};

} // namespace sanguosha
