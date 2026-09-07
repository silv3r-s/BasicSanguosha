#include "network/NetworkProtocol.h"

#include <QDataStream>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace sanguosha::network {
namespace {
QJsonObject cardToJson(const CardView& c) { QJsonObject o{{"id",QString::fromStdString(c.id)},{"type",int(c.type)},{"suit",int(c.suit)},{"rank",c.rank},{"name",QString::fromStdString(c.displayName)},{"minTargets",c.minTargets},{"maxTargets",c.maxTargets}}; if(c.attackRange) o["range"]=*c.attackRange; return o; }
std::optional<CardView> cardFromJson(const QJsonObject& o) { if(!o.contains("id")) return {}; CardView c{o["id"].toString().toStdString(),CardType(o["type"].toInt()),Suit(o["suit"].toInt()),o["rank"].toInt(),o["name"].toString().toStdString(),{}}; if(o.contains("range")) c.attackRange=o["range"].toInt(); if(o.contains("minTargets")) c.minTargets=std::clamp(o["minTargets"].toInt(), 0, 3); if(o.contains("maxTargets")) c.maxTargets=std::clamp(o["maxTargets"].toInt(), 1, 3); if(c.minTargets>c.maxTargets) return {}; return c; }
QJsonArray cardsToJson(const std::vector<CardView>& cards) { QJsonArray a; for(const auto& c:cards) a.append(cardToJson(c)); return a; }
std::optional<std::vector<CardView>> cardsFromJson(const QJsonArray& a) { std::vector<CardView> result; for(const auto& value:a) { auto c=cardFromJson(value.toObject()); if(!c) return {}; result.push_back(*c); } return result; }
QJsonArray idsToJson(const std::vector<PlayerId>& ids) { QJsonArray a; for(auto id:ids)a.append(id); return a; }
std::vector<PlayerId> idsFromJson(const QJsonArray& a) { std::vector<PlayerId> ids; for(const auto& v:a) ids.push_back(v.toInt()); return ids; }
constexpr int kEquipmentEffectTypeCount = 8;
bool isEquipmentEffectType(int value) { return value >= 0 && value < kEquipmentEffectTypeCount; }
bool isCardType(int value) { return value >= int(CardType::Slash) && value <= int(CardType::DefensiveHorse); }
std::optional<std::uint64_t> eventIdFromJson(const QJsonValue& value)
{
    // QJsonValue stores JSON numbers as doubles, which cannot preserve every
    // uint64_t. Equipment event IDs therefore use an exact decimal string.
    if (!value.isString()) return {};
    bool ok = false;
    const auto result = value.toString().toULongLong(&ok);
    if (!ok || result == 0) return {};
    return std::uint64_t(result);
}
}

QByteArray MessageCodec::frame(const QJsonObject& object) { const QByteArray payload=QJsonDocument(object).toJson(QJsonDocument::Compact); QByteArray result(4, Qt::Uninitialized); const quint32 length=payload.size(); result[0]=char((length>>24)&0xff); result[1]=char((length>>16)&0xff); result[2]=char((length>>8)&0xff); result[3]=char(length&0xff); return result+payload; }
bool MessageCodec::append(const QByteArray& data, std::vector<QJsonObject>& messages, QString& error) { buffer_+=data; while(buffer_.size()>=4) { const quint32 n=(quint32(uchar(buffer_[0]))<<24)|(quint32(uchar(buffer_[1]))<<16)|(quint32(uchar(buffer_[2]))<<8)|quint32(uchar(buffer_[3])); if(n>MaxMessageSize){error=QStringLiteral("消息长度超出限制。");return false;} if(buffer_.size()<4+int(n)) return true; const QByteArray payload=buffer_.mid(4,n); buffer_.remove(0,4+n); QJsonParseError e; const auto doc=QJsonDocument::fromJson(payload,&e); if(e.error!=QJsonParseError::NoError||!doc.isObject()){error=QStringLiteral("收到非法 JSON 消息。");return false;} messages.push_back(doc.object()); } return true; }

QJsonObject encodeViewState(const PlayerViewState& v)
{
    QJsonObject o{{"self",v.selfPlayerId},{"selfIdentity",int(v.selfIdentity)},{"turn",v.turnNumber},{"current",v.currentTurnPlayer},{"phase",int(v.currentPhase)},{"gameMode",int(v.gameMode)},{"draw",qint64(v.drawPileCount)},{"discard",qint64(v.discardPileCount)},{"over",v.gameOver},{"winningSide",int(v.winningSide)},{"ownHand",cardsToJson(v.ownHand)},{"serpentSpearTargets",idsToJson(v.serpentSpearSlashTargets)}};
    if(v.winner)o["winner"]=*v.winner;
    QJsonArray winners; for(const auto id:v.winningPlayers) winners.append(id); o["winningPlayers"]=winners;
    QJsonArray ps;
    for(const auto& p:v.players) {
        QJsonObject po{{"id",p.id},{"name",QString::fromStdString(p.displayName)},{"hp",p.hp},{"maxHp",p.maxHp},{"alive",p.alive},{"dying",p.dying},{"connected",p.connected},{"chained",p.chained},{"handCount",qint64(p.handCardCount)},{"seat",p.seat},{"control",int(p.controlType)}};
        if(p.identity)po["identity"]=int(*p.identity);
        QJsonArray eq; for(const auto& c:p.equipment.cardsBySlot) eq.append(c?cardToJson(*c):QJsonValue()); po["equipment"]=eq;
        po["judgment"]=cardsToJson(p.judgmentCards); ps.append(po);
    }
    o["players"]=ps;
    if(v.response){const auto&r=*v.response;o["response"]=QJsonObject{{"type",int(r.type)},{"request",qint64(r.requestId)},{"requester",r.requester},{"responder",r.responder},{"isResponder",r.isResponder},{"decline",r.allowDecline},{"cards",cardsToJson(r.selectableCards)},{"prompt",QString::fromStdString(r.prompt)},{"target",r.targetId}};}
    if(v.cardSelection){const auto&s=*v.cardSelection;QJsonArray options;for(const auto& x:s.options) { QJsonObject option{{"id",qint64(x.optionId)},{"zone",int(x.zone)},{"hidden",x.hidden},{"name",QString::fromStdString(x.displayName)},{"slot",x.equipmentSlot?int(*x.equipmentSlot):-1}}; if(x.cardType) option["cardType"] = int(*x.cardType); options.append(option); } o["selection"]=QJsonObject{{"request",qint64(s.requestId)},{"purpose",int(s.purpose)},{"target",s.targetId},{"min",s.minCount},{"max",s.maxCount},{"prompt",QString::fromStdString(s.prompt)},{"options",options}};}
    if(v.harvest)o["harvest"]=QJsonObject{{"picker",v.harvest->currentPicker},{"pool",cardsToJson(v.harvest->pool)}};
    if(v.judgment){const auto&j=*v.judgment;o["judgmentResult"]=QJsonObject{{"player",j.playerId},{"trick",QString::fromStdString(j.delayedTrickId)},{"trickType",int(j.delayedTrickType)},{"card",cardToJson(j.card)},{"success",j.succeeded}};}
    if(v.fireAttack)o["fireAttack"]=QJsonObject{{"target",v.fireAttack->targetId},{"card",cardToJson(v.fireAttack->revealedCard)}};
    if(v.nullification){const auto& n=*v.nullification; QJsonObject context{{"source",n.sourceId},{"trickType",int(n.trickType)},{"round",n.chainRound}}; if(n.targetId) context["target"] = *n.targetId; o["nullification"] = context;}
    QJsonArray equipmentEffects;
    for (const auto& event : v.equipmentEffects) {
        equipmentEffects.append(QJsonObject{{"eventId", QString::number(qulonglong(event.eventId))}, {"equipment", QString::fromStdString(event.equipment)},
                                             {"effect", int(event.effect)}, {"owner", event.ownerId}, {"target", event.targetId},
                                             {"value", event.value}, {"relatedCard", int(event.relatedCard)}});
    }
    o["equipmentEffects"] = equipmentEffects;
    QJsonArray logs;for(const auto& x:v.visibleLogs)logs.append(QString::fromStdString(x));o["logs"]=logs;return o;
}

std::optional<PlayerViewState> decodeViewState(const QJsonObject& o)
{
    if (!o.contains("self") || !o.contains("players")) return {};
    PlayerViewState v{o["self"].toInt(), o["turn"].toInt(), o["current"].toInt(), Phase(o["phase"].toInt())};
    if (!o.contains("gameMode") || !o["gameMode"].isDouble()) return {};
    const int mode = o["gameMode"].toInt();
    if (mode != int(GameMode::FreeForAll) && mode != int(GameMode::Identity)) return {};
    v.gameMode = GameMode(mode);
    const int selfIdentity = o["selfIdentity"].toInt();
    if (selfIdentity < int(PlayerIdentity::None) || selfIdentity > int(PlayerIdentity::Renegade)) return {};
    v.selfIdentity = PlayerIdentity(selfIdentity);
    const auto own = cardsFromJson(o["ownHand"].toArray()); if (!own) return {}; v.ownHand = *own;
    if (o.contains("serpentSpearTargets")) { if (!o["serpentSpearTargets"].isArray()) return {}; v.serpentSpearSlashTargets = idsFromJson(o["serpentSpearTargets"].toArray()); }
    for (const auto& value : o["players"].toArray()) {
        const auto p = value.toObject();
        PublicPlayerView pv{p["id"].toInt(), p["name"].toString().toStdString(), p["hp"].toInt(), p["maxHp"].toInt(), p["alive"].toBool(), p["dying"].toBool(), p.contains("connected") ? p["connected"].toBool() : true, p["chained"].toBool(), size_t(p["handCount"].toInteger()), {}};
        pv.seat = p["seat"].toInt();
        if (p.contains("control")) { if (!p["control"].isDouble()) return {}; const int control = p["control"].toInt(); if (control != int(PlayerControlType::Human) && control != int(PlayerControlType::AI)) return {}; pv.controlType = PlayerControlType(control); }
        if (p.contains("identity")) { const int identity = p["identity"].toInt(); if (identity < int(PlayerIdentity::None) || identity > int(PlayerIdentity::Renegade)) return {}; pv.identity = PlayerIdentity(identity); }
        const auto equipment = p["equipment"].toArray();
        for (int i = 0; i < equipment.size() && i < 4; ++i) if (!equipment[i].isNull()) { const auto card = cardFromJson(equipment[i].toObject()); if (!card) return {}; pv.equipment.cardsBySlot[i] = *card; }
        if (p.contains("judgment")) { const auto judgment = cardsFromJson(p["judgment"].toArray()); if (!judgment) return {}; pv.judgmentCards = *judgment; }
        v.players.push_back(std::move(pv));
    }
    v.drawPileCount = size_t(o["draw"].toInteger()); v.discardPileCount = size_t(o["discard"].toInteger()); v.gameOver = o["over"].toBool(); v.winningSide = WinningSide(o["winningSide"].toInt());
    for (const auto& id : o["winningPlayers"].toArray()) v.winningPlayers.push_back(id.toInt()); if (o.contains("winner")) v.winner = o["winner"].toInt();
    if (o.contains("response")) { const auto r = o["response"].toObject(); const auto cards = cardsFromJson(r["cards"].toArray()); if (!cards) return {}; v.response = ResponseView{ResponseType(r["type"].toInt()), uint64_t(r["request"].toInteger()), r["requester"].toInt(), r["responder"].toInt(), r["isResponder"].toBool(), r["decline"].toBool(), *cards, r["prompt"].toString().toStdString(), r["target"].toInt()}; }
    if (o.contains("selection")) { const auto s = o["selection"].toObject(); CardSelectionView selection{uint64_t(s["request"].toInteger()), CardSelectionPurpose(s["purpose"].toInt()), s["target"].toInt(), s["min"].toInt(1), s["max"].toInt(1), {}, s["prompt"].toString().toStdString()}; for (const auto& value : s["options"].toArray()) { const auto option = value.toObject(); const int slot = option["slot"].toInt(-1); std::optional<CardType> cardType; if (option.contains("cardType")) { if (!option["cardType"].isDouble()) return {}; cardType = CardType(option["cardType"].toInt()); } selection.options.push_back({uint64_t(option["id"].toInteger()), CardZone(option["zone"].toInt()), slot < 0 ? std::nullopt : std::optional<EquipmentSlot>(EquipmentSlot(slot)), option["hidden"].toBool(), option["name"].toString().toStdString(), cardType}); } v.cardSelection = std::move(selection); }
    if (o.contains("harvest")) { const auto harvest = o["harvest"].toObject(); const auto pool = cardsFromJson(harvest["pool"].toArray()); if (!pool) return {}; v.harvest = HarvestView{harvest["picker"].toInt(), *pool}; }
    if (o.contains("judgmentResult")) { const auto j = o["judgmentResult"].toObject(); const auto card = cardFromJson(j["card"].toObject()); if (!card) return {}; v.judgment = JudgmentView {j["player"].toInt(), j["trick"].toString().toStdString(), CardType(j["trickType"].toInt()), *card, j["success"].toBool()}; }
    if (o.contains("fireAttack")) { const auto fire = o["fireAttack"].toObject(); const auto card = cardFromJson(fire["card"].toObject()); if (!card) return {}; v.fireAttack = FireAttackView {fire["target"].toInt(), *card}; }
    if (o.contains("nullification")) { const auto n = o["nullification"].toObject(); if (!n["source"].isDouble() || !n["trickType"].isDouble() || !n["round"].isDouble()) return {}; NullificationContextView context {n["source"].toInt(), CardType(n["trickType"].toInt()), {}, n["round"].toInt()}; if (n.contains("target")) { if (!n["target"].isDouble()) return {}; context.targetId = n["target"].toInt(); } v.nullification = context; }
    if (o.contains("equipmentEffects")) {
        if (!o["equipmentEffects"].isArray()) return {};
        std::vector<EquipmentEffectEventView> effects;
        const auto array = o["equipmentEffects"].toArray();
        effects.reserve(array.size());
        std::uint64_t previousId = 0;
        for (const auto& value : array) {
            if (!value.isObject()) return {};
            const auto event = value.toObject();
            if (!event.contains("eventId") || !event.contains("equipment") || !event.contains("effect") || !event.contains("owner")
                || !event.contains("target") || !event.contains("value") || !event.contains("relatedCard")
                || !event["equipment"].isString() || !event["effect"].isDouble() || !event["owner"].isDouble()
                || !event["target"].isDouble() || !event["value"].isDouble() || !event["relatedCard"].isDouble()) return {};
            const auto eventId = eventIdFromJson(event["eventId"]);
            const int effect = event["effect"].toInt();
            const int relatedCard = event["relatedCard"].toInt();
            if (!eventId || *eventId <= previousId || !isEquipmentEffectType(effect) || !isCardType(relatedCard)) return {};
            previousId = *eventId;
            effects.push_back({*eventId, event["equipment"].toString().toStdString(), EquipmentEffectTypeView(effect),
                               event["owner"].toInt(), event["target"].toInt(), event["value"].toInt(), CardType(relatedCard)});
        }
        v.equipmentEffects = std::move(effects);
    }
    for (const auto& entry : o["logs"].toArray()) v.visibleLogs.push_back(entry.toString().toStdString());
    return v;
}

QJsonObject encodeAction(const GameAction& a,std::optional<std::vector<SelectionOptionId>> options){QJsonObject o;std::visit([&](const auto& x){o["player"]=x.playerId;using T=std::decay_t<decltype(x)>;if constexpr(std::is_same_v<T,EndPlayPhaseAction>)o["kind"]="end";else if constexpr(std::is_same_v<T,PlayCardAction>){o["kind"]="play";o["card"]=QString::fromStdString(x.cardId);o["targets"]=idsToJson(x.targetIds);}else if constexpr(std::is_same_v<T,PlayVirtualSlashAction>){o["kind"]="virtual_slash";QJsonArray ids;for(const auto&id:x.subcardIds)ids.append(QString::fromStdString(id));o["subcards"]=ids;o["targets"]=idsToJson(x.targetIds);}else if constexpr(std::is_same_v<T,RespondVirtualSlashAction>){o["kind"]="virtual_slash_response";o["request"]=qint64(x.requestId);QJsonArray ids;for(const auto&id:x.subcardIds)ids.append(QString::fromStdString(id));o["subcards"]=ids;}else if constexpr(std::is_same_v<T,DiscardAction>){o["kind"]="discard";QJsonArray ids;for(const auto&id:x.cardIds)ids.append(QString::fromStdString(id));o["cards"]=ids;}else if constexpr(std::is_same_v<T,RespondAction>){o["kind"]="respond";o["request"]=qint64(x.requestId);if(x.cardId)o["card"]=QString::fromStdString(*x.cardId);}else{o["kind"]="select";o["request"]=qint64(x.requestId);QJsonArray ids;for(const auto id:options.value_or(std::vector<SelectionOptionId>{}))ids.append(qint64(id));o["options"]=ids;}} ,a);return o;}
std::optional<DecodedAction> decodeAction(const QJsonObject&o){
    if (!o.contains("kind") || !o["kind"].isString() || !o.contains("player") || !o["player"].isDouble()) return {};
    const auto kind=o["kind"].toString();const auto player=o["player"].toInt();
    if(kind=="end")return DecodedAction{EndPlayPhaseAction{player},{}};
    if(kind=="play") { if(!o["card"].isString() || !o["targets"].isArray()) return {}; return DecodedAction{PlayCardAction{player,o["card"].toString().toStdString(),idsFromJson(o["targets"].toArray())},{}}; }
    if(kind=="virtual_slash") { if(!o["subcards"].isArray() || !o["targets"].isArray()) return {}; std::vector<CardId> cards; for(const auto& x:o["subcards"].toArray()){if(!x.isString())return{};cards.push_back(x.toString().toStdString());} return DecodedAction{PlayVirtualSlashAction{player,cards,idsFromJson(o["targets"].toArray())},{}}; }
    if(kind=="virtual_slash_response") { if(!o["request"].isDouble() || !o["subcards"].isArray()) return {}; std::vector<CardId> cards; for(const auto& x:o["subcards"].toArray()){if(!x.isString())return{};cards.push_back(x.toString().toStdString());} return DecodedAction{RespondVirtualSlashAction{player,uint64_t(o["request"].toInteger()),cards},{}}; }
    if(kind=="discard"){if(!o["cards"].isArray())return{};std::vector<CardId> ids;for(const auto&x:o["cards"].toArray()){if(!x.isString())return{};ids.push_back(x.toString().toStdString());}return DecodedAction{DiscardAction{player,ids},{}};}
    if(kind=="respond"){if(!o["request"].isDouble())return{};std::optional<CardId> c;if(o.contains("card")){if(!o["card"].isString())return{};c=o["card"].toString().toStdString();}return DecodedAction{RespondAction{player,uint64_t(o["request"].toInteger()),c},{}};}
    if(kind=="select"&&o["request"].isDouble()&&o["options"].isArray()){std::vector<SelectionOptionId> ids;for(const auto&value:o["options"].toArray()){if(!value.isDouble())return{};ids.push_back(uint64_t(value.toInteger()));}return DecodedAction{SelectCardsAction{player,uint64_t(o["request"].toInteger()),{}},ids};}return{};}
QJsonObject encodeActionResult(uint64_t id,const ActionResult&r){return{{"requestId",qint64(id)},{"ok",r.accepted},{"error",int(r.error)}};}
bool decodeActionResult(const QJsonObject&o,uint64_t&id,ActionResult&r){if(!o.contains("requestId")||!o["requestId"].isDouble())return false;id=uint64_t(o["requestId"].toInteger());r.accepted=o["ok"].toBool();r.error=ActionResult::Error(o["error"].toInt());return true;}
} // namespace sanguosha::network
