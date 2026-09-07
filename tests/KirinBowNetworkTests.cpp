#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <cstdlib>
#include <iostream>
#include "network/GameServer.h"
#include "network/NetworkGameClient.h"
#include "client/GameClientController.h"
using namespace sanguosha;
using namespace sanguosha::network;
namespace {
[[noreturn]] void fail(const char* m) { std::cerr << "FAIL: " << m << '\n'; std::exit(1); }
void ok(bool v, const char* m) { if (!v) fail(m); }
bool waitFor(const std::function<bool()>& f, int ms=3000) { if(f()) return true; QEventLoop l; QTimer t,p; t.setSingleShot(true); QObject::connect(&t,&QTimer::timeout,&l,&QEventLoop::quit); QObject::connect(&p,&QTimer::timeout,&l,[&]{if(f())l.quit();}); t.start(ms);p.start(10);l.exec();return f(); }
struct Fixture {
 GameServer server; GameClientController host; std::unique_ptr<NetworkGameClient> actor, observer;
 Fixture():host(server.session(),1) {
  QString e; ok(server.listen(e,0)&&server.setTargetPlayerCount(3),"server starts");
  actor=std::make_unique<NetworkGameClient>("127.0.0.1",server.serverPort()); actor->connectToHost();
  ok(waitFor([&]{return server.connectedHumanCount()==2&&actor->selfPlayerId()==2;}),"actor receives seat P2");
  observer=std::make_unique<NetworkGameClient>("127.0.0.1",server.serverPort()); observer->connectToHost();
  ok(waitFor([&]{return server.connectedHumanCount()==3&&observer->selfPlayerId()==3;})&&server.startLobbyGame()&&waitFor([&]{return actor->connected()&&observer->connected();}),"TCP clients start");
  auto& x=server.session().testingEngine(); for(auto&p:x.players()){std::vector<CardId> ids;for(auto&c:p->handCards())ids.push_back(c->id());for(auto&i:ids)p->removeCard(i);} x.testingEquip(2,std::make_shared<Card>("bow", "\xE9\xBA\x92\xE9\xBA\x9F\xE5\xBC\x93",CardType::Weapon,Suit::Spade,1,EquipmentData{EquipmentSlot::Weapon,5})); x.testingEquip(3,std::make_shared<Card>("mount","mount",CardType::OffensiveHorse,Suit::Heart,1,EquipmentData{EquipmentSlot::OffensiveHorse,1})); x.players()[1]->addCard(std::make_shared<Card>("SECRET_KIRIN_NETWORK_SLASH","secret",CardType::Slash,Suit::Club,1));
  ok(host.submit(EndPlayPhaseAction{1}).accepted,"advance remote actor turn"); server.broadcastViews(); ok(waitFor([&]{return actor->view().currentTurnPlayer==2&&actor->view().currentPhase==Phase::Play;}),"actor Play phase");
  ok(actor->submit(PlayCardAction{2,"SECRET_KIRIN_NETWORK_SLASH",{3}}).accepted,"remote actor submits Slash");
  ok(waitFor([&]{return x.pendingResponse().has_value();}),"target response request arrives"); const auto dodge=*x.pendingResponse(); ok(dodge.responder==3&&observer->submit(RespondAction{3,dodge.requestId,std::nullopt}).accepted,"remote target passes Dodge");
  ok(waitFor([&]{return x.pendingCardSelection().has_value();}),"Kirin request exists");
  server.broadcastViews(); ok(waitFor([&]{return actor->view().cardSelection.has_value();}),"Kirin selection reaches actor");
 }
};
void kirinBowNetworkActorReceivesSelection(){Fixture f;const auto&s=*f.actor->view().cardSelection;ok(s.purpose==CardSelectionPurpose::KirinBowMount&&s.targetId==3&&s.minCount==0&&s.maxCount==1&&s.options.size()==1&&s.options[0].equipmentSlot==EquipmentSlot::OffensiveHorse,"actor receives authoritative Kirin selection");}
void kirinBowNetworkNonActorCannotInteract(){Fixture f;auto id=f.actor->view().cardSelection->requestId; ok(!f.observer->view().cardSelection,"non actor has no selection");f.observer->submitCardSelection(id,1);ok(waitFor([&]{return !f.observer->actionPending();})&&f.server.session().testingEngine().pendingCardSelection().has_value()&&f.server.session().testingEngine().player(3)->equipment(EquipmentSlot::OffensiveHorse),"non actor rejected without mount loss");}
void kirinBowNetworkDoesNotLeakPrivateHand(){Fixture f;QByteArray json=QJsonDocument(f.server.viewPayloadFor(3)).toJson(QJsonDocument::Compact);ok(!json.contains("SECRET_KIRIN_NETWORK_SLASH")&&!f.observer->view().cardSelection,"Kirin payload hides actor private hand and selection");}
void kirinBowNetworkRejectsStaleRequest(){Fixture f;auto id=f.actor->view().cardSelection->requestId;f.actor->submitCardSelection(id,std::vector<SelectionOptionId>{});ok(waitFor([&]{return !f.actor->actionPending()&&!f.server.session().testingEngine().pendingCardSelection();}),"actor passes Kirin");f.actor->submitCardSelection(id,1);ok(waitFor([&]{return !f.actor->actionPending();})&&f.server.session().testingEngine().player(3)->equipment(EquipmentSlot::OffensiveHorse)&&!f.server.session().testingEngine().kirinBowContext(),"stale Kirin request rejected");}
}
int main(int argc,char**argv){QCoreApplication app(argc,argv);kirinBowNetworkActorReceivesSelection();kirinBowNetworkNonActorCannotInteract();kirinBowNetworkDoesNotLeakPrivateHand();kirinBowNetworkRejectsStaleRequest();std::cout<<"BasicSanguoshaKirinBowNetworkTests PASS\n";}
