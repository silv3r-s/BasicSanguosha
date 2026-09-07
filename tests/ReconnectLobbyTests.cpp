#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QTimer>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include "network/GameServer.h"
#include "network/NetworkGameClient.h"
using namespace sanguosha; using namespace sanguosha::network;
namespace { [[noreturn]] void fail(const char*m){std::cerr<<"FAIL: "<<m<<'\n';std::exit(1);} void ok(bool x,const char*m){if(!x)fail(m);} bool waitFor(const std::function<bool()>&f,int ms=3000){if(f())return true;QEventLoop l;QTimer t,p;t.setSingleShot(true);QObject::connect(&t,&QTimer::timeout,&l,&QEventLoop::quit);QObject::connect(&p,&QTimer::timeout,&l,[&]{if(f())l.quit();});t.start(ms);p.start(10);l.exec();return f();}
struct F { GameServer s; std::unique_ptr<NetworkGameClient> c; F(){QString e;ok(s.listen(e,0)&&s.setTargetPlayerCount(3),"server");c=std::make_unique<NetworkGameClient>("127.0.0.1",s.serverPort(),"Alice");c->connectToHost();ok(waitFor([&]{return c->selfPlayerId()==2&&!c->reconnectToken().isEmpty();}),"welcome");} ~F(){c->disconnectFromHost();waitFor([&]{return s.connectedHumanCount()==1;});QCoreApplication::processEvents();s.close();}};
void initialJoinReceivesReconnectToken(){F f;const auto t=f.c->reconnectToken();ok(t.size()>20&&t!=QString::number(f.c->selfPlayerId()),"random token");ok(!QJsonDocument(f.s.viewPayloadFor(1)).toJson(QJsonDocument::Compact).contains(t.toUtf8()),"token private");}
void reconnectRestoresOriginalSeat(){F f;const auto t=f.c->reconnectToken();const auto seat=f.c->selfPlayerId();f.c->disconnectForReconnect();ok(waitFor([&]{return f.s.connectedHumanCount()==1;}),"server sees disconnect");f.c->reconnectToHost();ok(waitFor([&]{return f.c->selfPlayerId()==seat&&f.c->reconnectToken()==t&&f.s.connectedHumanCount()==2;}),"same seat restored");}
void lobbyPlayerListTracksReconnectLifecycle(){
    F f; f.s.setHostDisplayName(QStringLiteral("Host"));
    NetworkGameClient observer(QStringLiteral("127.0.0.1"),f.s.serverPort(),QStringLiteral("Bob")); observer.connectToHost();
    const auto aliceId=f.c->selfPlayerId(); const auto aliceSeat=aliceId-1;
    const auto findPlayer=[](const LobbyState& state,PlayerId id){return std::find_if(state.players.begin(),state.players.end(),[id](const auto& p){return p.playerId==id;});};
    ok(waitFor([&]{const auto& state=observer.lobbyState();const auto host=findPlayer(state,1);const auto alice=findPlayer(state,aliceId);return state.players.size()==3&&host!=state.players.end()&&host->host&&host->nickname==QStringLiteral("Host")&&alice!=state.players.end()&&alice->nickname==QStringLiteral("Alice")&&alice->seat==aliceSeat&&alice->connected&&!alice->ai;}),"authoritative host and remote lobby rosters include real human seats");
    f.c->disconnectForReconnect();
    ok(waitFor([&]{const auto it=findPlayer(observer.lobbyState(),aliceId);return f.s.testingReconnectReservationCount()==1&&it!=observer.lobbyState().players.end()&&it->nickname==QStringLiteral("Alice")&&it->seat==aliceSeat&&!it->connected;}),"observer receives the reserved offline lobby entry");
    f.c->reconnectToHost();
    ok(waitFor([&]{const auto it=findPlayer(observer.lobbyState(),aliceId);return f.s.testingReconnectReservationCount()==0&&it!=observer.lobbyState().players.end()&&it->nickname==QStringLiteral("Alice")&&it->seat==aliceSeat&&it->connected&&std::count_if(observer.lobbyState().players.begin(),observer.lobbyState().players.end(),[aliceId](const auto& p){return p.playerId==aliceId;})==1;}),"reconnect restores one authoritative online entry at the original seat");
    f.s.setReconnectGracePeriodForTesting(50); f.c->disconnectForReconnect();
    ok(waitFor([&]{const auto it=findPlayer(observer.lobbyState(),aliceId);return f.s.testingReconnectReservationCount()==0&&it!=observer.lobbyState().players.end()&&it->ai&&it->nickname==QStringLiteral("AI-1");}),"grace cleanup replaces the released human seat with an AI lobby entry");
    observer.disconnectFromHost(); ok(waitFor([&]{return f.s.connectedHumanCount()==1;}),"observer leaves cleanly");
}
void completedGameReturnsToReusableLobby(){
    F f; f.s.setHostDisplayName(QStringLiteral("Host")); const auto token=f.c->reconnectToken(); const auto seat=f.c->selfPlayerId();
    ok(f.s.startLobbyGame()&&waitFor([&]{return f.c->lobbyState().gameStarted&&f.c->connected()&&f.c->view().players.size()==3;}),"first game starts over the existing TCP connection");
    auto& first=f.s.session().testingEngine(); first.testingSetHp(2,1); first.testingSetChained(2,true); first.testingKillPlayer(1); first.testingKillPlayer(3); f.s.broadcastViews();
    ok(waitFor([&]{return first.gameOver()&&f.c->view().gameOver&&f.c->view().players.at(1).chained;}),"completed first game reaches the remote client");
    ok(f.s.returnToLobby(),"host returns completed game to lobby");
    const auto findPlayer=[](const LobbyState& state,PlayerId id){return std::find_if(state.players.begin(),state.players.end(),[id](const auto& p){return p.playerId==id;});};
    ok(waitFor([&]{const auto it=findPlayer(f.c->lobbyState(),seat);const auto& view=f.c->view();return !f.c->lobbyState().gameStarted&&it!=f.c->lobbyState().players.end()&&it->seat==seat-1&&it->nickname==QStringLiteral("Alice")&&it->connected&&!view.gameOver&&!view.response&&!view.cardSelection&&view.players.empty()&&view.timeoutKind==TimeoutKind::None;}),"lobby reset clears terminal and interaction ViewState while preserving the authoritative roster");
    f.c->disconnectForReconnect(); ok(waitFor([&]{return f.s.testingReconnectReservationCount()==1;}),"completed-game lobby disconnect keeps the reconnect reservation"); f.c->reconnectToHost();
    ok(waitFor([&]{return f.c->selfPlayerId()==seat&&f.c->reconnectToken()==token&&!f.c->lobbyState().gameStarted&&f.s.testingReconnectReservationCount()==0&&f.c->view().players.empty()&&!f.c->view().gameOver;}),"lobby reconnect restores only the room seat, not the completed game");
    ok(f.s.startLobbyGame()&&waitFor([&]{const auto& view=f.c->view();return f.c->lobbyState().gameStarted&&view.players.size()==3&&!view.gameOver&&!view.response&&!view.cardSelection&&view.timeoutKind==TimeoutKind::Play&&view.currentTurnPlayer==1&&view.turnNumber==1&&view.players.at(1).alive&&view.players.at(1).hp==view.players.at(1).maxHp&&!view.players.at(1).chained&&!view.players.at(1).equipment.cardsBySlot[0]&&view.ownHand.size()==4;}),"second game starts from clean state on the same client binding");
    ok(f.s.returnToLobby()&&waitFor([&]{return !f.c->lobbyState().gameStarted&&f.c->view().players.empty();}),"first repeated return reaches the clean lobby");
    ok(f.s.startLobbyGame()&&waitFor([&]{return f.c->lobbyState().gameStarted&&f.c->view().turnNumber==1&&f.c->view().players.size()==3&&!f.c->view().gameOver&&!f.c->view().response&&!f.c->view().cardSelection&&f.s.testingReconnectReservationCount()==0;}),"second repeated start has no stale interaction or reservation");
}
}
int main(int argc,char**argv){QCoreApplication app(argc,argv);std::cout<<"[RUN] initialJoinReceivesReconnectToken\n";initialJoinReceivesReconnectToken();std::cout<<"[PASS] initialJoinReceivesReconnectToken\n[RUN] reconnectRestoresOriginalSeat\n";reconnectRestoresOriginalSeat();std::cout<<"[PASS] reconnectRestoresOriginalSeat\n[RUN] lobbyPlayerListTracksReconnectLifecycle\n";lobbyPlayerListTracksReconnectLifecycle();std::cout<<"[PASS] lobbyPlayerListTracksReconnectLifecycle\n[RUN] completedGameReturnsToReusableLobby\n";completedGameReturnsToReusableLobby();std::cout<<"[PASS] completedGameReturnsToReusableLobby\nBasicSanguoshaReconnectLobbyTests PASS\n";}
