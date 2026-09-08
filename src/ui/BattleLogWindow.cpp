#include "ui/BattleLogWindow.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStringList>
#include <QVBoxLayout>

namespace sanguosha::ui {

BattleLogWindow::BattleLogWindow(QWidget* parent) : QWidget(parent, Qt::Window)
{
    setObjectName(QStringLiteral("battleLogWindow"));
    setWindowTitle(tr("对战日志"));
    setWindowModality(Qt::NonModal);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(520, 620);
    setMinimumSize(360, 300);

    auto* layout = new QVBoxLayout(this);
    logView_ = new QPlainTextEdit(this);
    logView_->setObjectName(QStringLiteral("battleLogView"));
    logView_->setReadOnly(true);
    logView_->setPlaceholderText(tr("当前暂无对战记录。"));
    layout->addWidget(logView_, 1);

    auto* actions = new QHBoxLayout;
    auto* copy = new QPushButton(tr("复制日志"), this);
    auto* latest = new QPushButton(tr("跳到最新"), this);
    copy->setObjectName(QStringLiteral("copyBattleLogButton"));
    latest->setObjectName(QStringLiteral("latestBattleLogButton"));
    actions->addStretch();
    actions->addWidget(copy);
    actions->addWidget(latest);
    layout->addLayout(actions);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(logView_->toPlainText()); });
    connect(latest, &QPushButton::clicked, this, [this] {
        logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
    });
}

void BattleLogWindow::setEntries(const QStringList& entries)
{
    const QString text = entries.join(QLatin1Char('\n'));
    if (text == cachedText_) return;
    cachedText_ = text;
    logView_->setPlainText(text);
    logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
}

void BattleLogWindow::clearEntries()
{
    cachedText_.clear();
    logView_->clear();
}

void BattleLogWindow::closeEvent(QCloseEvent* event)
{
    event->ignore();
    hide();
}

} // namespace sanguosha::ui
