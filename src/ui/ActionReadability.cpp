#include "ui/ActionReadability.h"

#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QStyle>

#include "ai/AIActionPacing.h"
#include "ui/GameText.h"

namespace sanguosha::ui {
namespace {
QString cardToken(QString token)
{
    token.replace(QStringLiteral("Fire Slash"), QStringLiteral("火杀"));
    token.replace(QStringLiteral("Thunder Slash"), QStringLiteral("雷杀"));
    token.replace(QStringLiteral("Ex Nihilo"), QStringLiteral("无中生有"));
    token.replace(QStringLiteral("Dismantlement"), QStringLiteral("过河拆桥"));
    token.replace(QStringLiteral("Snatch"), QStringLiteral("顺手牵羊"));
    token.replace(QStringLiteral("Duel"), QStringLiteral("决斗"));
    token.replace(QStringLiteral("Peach Garden"), QStringLiteral("桃园结义"));
    token.replace(QStringLiteral("Fire Attack"), QStringLiteral("火攻"));
    token.replace(QStringLiteral("Iron Chain"), QStringLiteral("铁索连环"));
    token.replace(QStringLiteral("Borrowed Sword"), QStringLiteral("借刀杀人"));
    token.replace(QStringLiteral("Barbarian Invasion"), QStringLiteral("南蛮入侵"));
    token.replace(QStringLiteral("Arrow Barrage"), QStringLiteral("万箭齐发"));
    token.replace(QStringLiteral("PeachGarden"), QStringLiteral("桃园结义"));
    token.replace(QStringLiteral("BarbarianInvasion"), QStringLiteral("南蛮入侵"));
    token.replace(QStringLiteral("ArrowBarrage"), QStringLiteral("万箭齐发"));
    token.replace(QStringLiteral("FireAttack"), QStringLiteral("火攻"));
    token.replace(QStringLiteral("IronChain"), QStringLiteral("铁索连环"));
    token.replace(QStringLiteral("BorrowedSword"), QStringLiteral("借刀杀人"));
    token.replace(QStringLiteral("Nullification"), QStringLiteral("无懈可击"));
    token.replace(QStringLiteral("Harvest"), QStringLiteral("五谷丰登"));
    token.replace(QStringLiteral("Dodge"), QStringLiteral("闪"));
    token.replace(QStringLiteral("Slash"), QStringLiteral("杀"));
    token.replace(QStringLiteral("Peach"), QStringLiteral("桃"));
    return token;
}
}

ActionReadability::ActionReadability(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("actionReadability"));
    auto* root = new QHBoxLayout(this); root->setContentsMargins(0, 0, 0, 0); root->setSpacing(8);
    auto* center = new QVBoxLayout;
    recent_ = new QPlainTextEdit(this); recent_->setObjectName(QStringLiteral("recentActionModelView")); recent_->setReadOnly(true); recent_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded); recent_->hide();
    latest_ = new QPushButton(QStringLiteral("回到最新"), this); latest_->setObjectName(QStringLiteral("recentActionLatest")); latest_->hide();
    stage_ = new QFrame(this); stage_->setObjectName(QStringLiteral("actionStage")); stage_->setMinimumHeight(230); stage_->setMaximumHeight(360); auto* stageLayout = new QVBoxLayout(stage_); stageLayout->setContentsMargins(18, 14, 18, 14); stageLayout->setSpacing(10); stageTitle_ = new QLabel(QStringLiteral("等待下一项重要行动"), stage_); stageTitle_->setObjectName(QStringLiteral("actionStageTitle")); stageTitle_->setAlignment(Qt::AlignCenter); stageTitle_->setWordWrap(true);
    auto* route = new QHBoxLayout; stageSourceLabel_ = new QLabel(QStringLiteral("[来源玩家]"), stage_); stageSourceLabel_->setObjectName(QStringLiteral("actionSource")); stageSourceLabel_->setAlignment(Qt::AlignCenter); stageCardLabel_ = new QLabel(QStringLiteral("【等待出牌】"), stage_); stageCardLabel_->setObjectName(QStringLiteral("displayCard")); stageCardLabel_->setAlignment(Qt::AlignCenter); stageCardLabel_->setMinimumSize(150, 126); stageTargetLabel_ = new QLabel(QStringLiteral("[目标玩家]"), stage_); stageTargetLabel_->setObjectName(QStringLiteral("actionTarget")); stageTargetLabel_->setAlignment(Qt::AlignCenter); route->addWidget(stageSourceLabel_, 1); route->addWidget(new QLabel(QStringLiteral("→"), stage_), 0); route->addWidget(stageCardLabel_); route->addWidget(new QLabel(QStringLiteral("→"), stage_), 0); route->addWidget(stageTargetLabel_, 1);
    stageDetail_ = new QLabel(QStringLiteral("已结算的出牌、目标与结果将在这里依次呈现"), stage_); stageDetail_->setObjectName(QStringLiteral("actionStageDetail")); stageDetail_->setAlignment(Qt::AlignCenter); stageDetail_->setWordWrap(true); stagePrompt_ = new QLabel(stage_); stagePrompt_->setObjectName(QStringLiteral("actionStagePrompt")); stagePrompt_->setAlignment(Qt::AlignCenter); stagePrompt_->setWordWrap(true); stageLayout->addWidget(stageTitle_); stageLayout->addLayout(route); stageLayout->addWidget(stageDetail_); stageLayout->addWidget(stagePrompt_); center->addWidget(stage_);
    root->addLayout(center, 1);
    auto* side = new QVBoxLayout; auto* compactTitle = new QLabel(QStringLiteral("对战日志"), this); compactTitle->setObjectName(QStringLiteral("compactLogTitle")); side->addWidget(compactTitle); compact_ = new QPlainTextEdit(this); compact_->setObjectName(QStringLiteral("compactBattleLog")); compact_->setReadOnly(true); compact_->setMinimumWidth(260); compact_->setMaximumWidth(320); compact_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded); side->addWidget(compact_); root->addLayout(side);
    timer_ = new QTimer(this); timer_->setSingleShot(true); connect(timer_, &QTimer::timeout, this, [this] { playing_ = false; playNext(); });
}

QString ActionReadability::playerName(const PlayerViewState& view, PlayerId id) const { for(const auto& p:view.players)if(p.id==id)return playerDisplayName(p.displayName);return QStringLiteral("玩家%1").arg(id); }
QString ActionReadability::playerSummary(const PlayerViewState& view, PlayerId id) const { for(const auto& p:view.players)if(p.id==id)return QStringLiteral("%1\n♥ %2/%3").arg(playerDisplayName(p.displayName)).arg(p.hp).arg(p.maxHp);return id?QStringLiteral("玩家%1").arg(id):QStringLiteral("—"); }

UIActionItem ActionReadability::present(const GameEvent& e, const PlayerViewState& view) const
{
    UIActionItem item; item.eventId=e.eventId; item.source=e.source.value_or(0); item.target=e.target.value_or(0);
    const auto source = playerName(view, item.source), target = playerName(view, item.target), card = cardToken(QString::fromStdString(e.detail));
    item.sourceText=playerSummary(view,item.source);item.targetText=playerSummary(view,item.target);
    const auto publicCardText=[&]{if(!e.card)return QStringLiteral("【%1】").arg(card);Card value("event-card",e.card->displayName,e.card->type,e.card->suit,e.card->rank);return QStringLiteral("【%1】\n%2%3\n%4").arg(cardDisplayName(value),suitToSymbol(e.card->suit),rankToDisplayName(e.card->rank),cardCategoryDisplayName(e.card->type));};
    if(e.type==GameEventType::CardUsed) { item.summary=item.target?QStringLiteral("%1 → %2 使用【%3】").arg(source,target,card):QStringLiteral("%1 使用【%2】").arg(source,card); item.cardText=publicCardText(); }
    else if(e.type==GameEventType::CardResponded) { item.summary=QStringLiteral("%1 打出【%2】").arg(source,card); item.cardText=publicCardText(); }
    else if(e.type==GameEventType::DamageReceived) { item.summary=QStringLiteral("%1 受到 %2 点伤害").arg(target,card); item.critical=true; }
    else if(e.type==GameEventType::HpRecovered) item.summary=QStringLiteral("%1 恢复 %2 点体力").arg(target,card);
    else if(e.type==GameEventType::PlayerDied) { item.summary=QStringLiteral("%1 死亡").arg(target); item.critical=true; }
    else if(e.type==GameEventType::GameEnded) { item.summary=QStringLiteral("游戏结束"); item.critical=true; }
    else return item;
    item.stageTitle=item.summary; item.stageDetail=item.target?QStringLiteral("来源：%1　目标：%2").arg(source,target):QStringLiteral("来源：%1").arg(source); return item;
}

void ActionReadability::appendBounded(QPlainTextEdit* view, const QString& text, int maximumBlocks)
{
    if(text.isEmpty())return; auto* bar=view->verticalScrollBar(); const bool follow=bar->value()>=bar->maximum()-2; view->appendPlainText(text); while(view->document()->blockCount()>maximumBlocks){QTextCursor cursor(view->document());cursor.select(QTextCursor::BlockUnderCursor);cursor.removeSelectedText();cursor.deleteChar();} if(follow)bar->setValue(bar->maximum());
}

void ActionReadability::enqueue(UIActionItem item)
{
    if(item.summary.isEmpty()||gameOver_)return; appendBounded(recent_,item.summary,40); appendBounded(compact_,item.summary,80); if(queue_.size()>=16){for(int i=0;i<queue_.size();++i)if(!queue_[i].critical){queue_.removeAt(i);break;}if(queue_.size()>=16)queue_.dequeue();} queue_.enqueue(std::move(item)); playNext();
}

void ActionReadability::playNext()
{
    if(playing_||queue_.isEmpty())return; playing_=true; const auto item=queue_.dequeue(); const auto speed=ai::AIActionPacing::preset(); const int duration=testingBypass_||speed==ai::AISpeedPreset::Test?0:(speed==ai::AISpeedPreset::Fast?(item.critical?650:450):speed==ai::AISpeedPreset::Slow?(item.critical?1450:1100):(item.critical?1050:780));stageSource_=item.source;stageTarget_=item.target;stage_->setProperty("sourcePlayer",item.source);stage_->setProperty("targetPlayer",item.target);stage_->setProperty("targetLine",item.target!=0);stage_->setProperty("critical",item.critical);stage_->style()->unpolish(stage_);stage_->style()->polish(stage_);stageTitle_->setText(item.stageTitle);stageSourceLabel_->setText(item.sourceText.isEmpty()?QStringLiteral("—"):item.sourceText);stageTargetLabel_->setText(item.targetText.isEmpty()?QStringLiteral("—"):item.targetText);stageCardLabel_->setText(item.cardText.isEmpty()?QStringLiteral("【结算】"):item.cardText);stageDetail_->setText(item.stageDetail+(item.critical?QStringLiteral("\n受击反馈：-1"):QString())); auto* effect=new QGraphicsOpacityEffect(stage_);stage_->setGraphicsEffect(effect);auto* group=new QParallelAnimationGroup(stage_);auto* opacity=new QPropertyAnimation(effect,"opacity",group);opacity->setDuration(duration);opacity->setStartValue(0.35);opacity->setKeyValueAt(0.25,1.0);opacity->setEndValue(0.82);auto* movement=new QPropertyAnimation(stageTitle_,"pos",group);movement->setDuration(duration/2);const auto destination=stageTitle_->pos();movement->setStartValue(destination-QPoint(48,0));movement->setEndValue(destination);group->start(QAbstractAnimation::DeleteWhenStopped);timer_->start(duration);
}

void ActionReadability::ingest(const PlayerViewState& view, bool reconnectSnapshot)
{
    if(reconnectSnapshot){queue_.clear();playing_=false;timer_->stop();recent_->clear();compact_->clear();for(const auto& event:view.publicEvents){const auto item=present(event,view);if(!item.summary.isEmpty()){appendBounded(recent_,item.summary,40);appendBounded(compact_,item.summary,80);}}if(!view.publicEvents.empty())lastEventId_=view.publicEvents.back().eventId;}
    for(const auto& event:view.publicEvents)if(event.eventId>lastEventId_){lastEventId_=event.eventId;auto item=present(event,view);if(!item.summary.isEmpty())enqueue(std::move(item));if(event.type==GameEventType::GameEnded){queue_.clear();gameOver_=true;stageTitle_->setText(QStringLiteral("游戏结束"));}}
    if(view.harvest)for(const auto& choice:view.harvest->choices)if(harvestChoices_.insert(choice.card.id).second){UIActionItem item;item.source=choice.playerId;item.sourceText=playerSummary(view,choice.playerId);item.cardText=QStringLiteral("【%1】\n%2%3\n公开牌").arg(cardTypeToDisplayName(choice.card.type),suitToSymbol(choice.card.suit),rankToDisplayName(choice.card.rank));item.summary=QStringLiteral("%1 从【五谷丰登】获得【%2 %3%4】").arg(playerName(view,choice.playerId),cardTypeToDisplayName(choice.card.type),suitToSymbol(choice.card.suit),rankToDisplayName(choice.card.rank));item.stageTitle=item.summary;item.stageDetail=QStringLiteral("公开选牌结果");enqueue(std::move(item));}
    if(view.fireAttack&&fireRevealId_!=view.fireAttack->revealedCard.id){fireRevealId_=view.fireAttack->revealedCard.id;UIActionItem item;item.target=view.fireAttack->targetId;item.targetText=playerSummary(view,item.target);item.cardText=QStringLiteral("【%1】\n%2%3\n展示牌").arg(cardTypeToDisplayName(view.fireAttack->revealedCard.type),suitToSymbol(view.fireAttack->revealedCard.suit),rankToDisplayName(view.fireAttack->revealedCard.rank));item.summary=QStringLiteral("%1 为【火攻】展示【%2 %3%4】").arg(playerName(view,view.fireAttack->targetId),cardTypeToDisplayName(view.fireAttack->revealedCard.type),suitToSymbol(view.fireAttack->revealedCard.suit),rankToDisplayName(view.fireAttack->revealedCard.rank));item.stageTitle=QStringLiteral("【火攻】公开展示");item.stageDetail=item.summary;enqueue(std::move(item));}
    for(const auto& effect:view.equipmentEffects)if(effect.eventId>lastEquipmentEventId_){lastEquipmentEventId_=effect.eventId;UIActionItem item;item.eventId=effect.eventId;item.source=effect.ownerId;item.target=effect.targetId;item.summary=formatEquipmentEffect(effect,playerName(view,effect.targetId));item.stageTitle=QStringLiteral("装备效果触发");item.stageDetail=item.summary;enqueue(std::move(item));}
    if(view.judgment&&judgmentId_!=view.judgment->card.id){judgmentId_=view.judgment->card.id;UIActionItem item;item.target=view.judgment->playerId;item.summary=QStringLiteral("%1 判定【%2 %3%4】%5").arg(playerName(view,view.judgment->playerId),cardTypeToDisplayName(view.judgment->card.type),suitToSymbol(view.judgment->card.suit),rankToDisplayName(view.judgment->card.rank),view.judgment->succeeded?QStringLiteral("成功"):QStringLiteral("失败"));item.stageTitle=QStringLiteral("判定结果");item.stageDetail=item.summary;enqueue(std::move(item));}
    if(reconnectSnapshot){queue_.clear();timer_->stop();playing_=false;stageTitle_->setText(QStringLiteral("已恢复当前对局"));stageDetail_->setText(QStringLiteral("旧动画已跳过，最近行动与交互状态已恢复"));}
}

void ActionReadability::resetForLobby(){queue_.clear();timer_->stop();playing_=false;gameOver_=false;lastEventId_=0;lastEquipmentEventId_=0;harvestChoices_.clear();fireRevealId_.clear();judgmentId_.clear();recent_->clear();compact_->clear();stageTitle_->setText(QStringLiteral("等待下一项重要行动"));stageSourceLabel_->setText(QStringLiteral("[来源玩家]"));stageCardLabel_->setText(QStringLiteral("【等待出牌】"));stageTargetLabel_->setText(QStringLiteral("[目标玩家]"));stageDetail_->setText(QStringLiteral("已结算的出牌、目标与结果将在这里依次呈现"));stagePrompt_->clear();stagePrompt_->hide();}
void ActionReadability::setTestingBypass(bool enabled){testingBypass_=enabled;}
void ActionReadability::setPrompt(const QString& prompt){stagePrompt_->setText(prompt);stagePrompt_->setVisible(!prompt.isEmpty());}

} // namespace sanguosha::ui
