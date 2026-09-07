#include "ui/InteractionPanel.h"

#include <algorithm>

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace sanguosha::ui {
namespace {
void clearLayout(QLayout* layout)
{
    while (auto* item = layout->takeAt(0)) { delete item->widget(); delete item; }
}

void resetClick(QPushButton* button, std::function<void()> action)
{
    QObject::disconnect(button, nullptr, nullptr, nullptr);
    if (action) QObject::connect(button, &QPushButton::clicked, button, [action = std::move(action)] { action(); });
}
}

InteractionPanel::InteractionPanel(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("interactionPanel"));
    setMinimumSize(0, 0);
    setProperty("overlay", true);
    setStyleSheet(QStringLiteral(
        "QFrame#interactionPanel{background:#fff8e7;border:2px solid #b98a45;border-radius:10px;}"
        "QLabel#interactionTitle{font-size:17px;font-weight:700;color:#5b321d;}"
        "QPushButton#interactionConfirm{background:#a83226;color:white;font-weight:700;border-radius:6px;}"
        "QPushButton#interactionConfirm:disabled{background:#aaa;color:#ddd;}"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 9, 12, 9);
    layout->setSpacing(5);
    title_ = new QLabel(this); title_->setObjectName(QStringLiteral("interactionTitle"));
    prompt_ = new QLabel(this); prompt_->setWordWrap(true);
    detail_ = new QLabel(this); detail_->setWordWrap(true); detail_->setStyleSheet(QStringLiteral("color:#705b45;"));
    layout->addWidget(title_); layout->addWidget(prompt_); layout->addWidget(detail_);

    auto* content = new QWidget(this);
    candidates_ = new QGridLayout(content);
    candidates_->setContentsMargins(5, 5, 5, 5);
    candidates_->setSpacing(8);
    candidates_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    scroll_ = new QScrollArea(this);
    scroll_->setObjectName(QStringLiteral("interactionCandidateScroll"));
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_->setWidget(content);
    layout->addWidget(scroll_, 1);

    auto* actions = new QHBoxLayout;
    confirm_ = new QPushButton(tr("确认"), this); confirm_->setObjectName(QStringLiteral("interactionConfirm"));
    cancel_ = new QPushButton(tr("取消选择"), this); cancel_->setObjectName(QStringLiteral("interactionCancel"));
    pass_ = new QPushButton(tr("跳过"), this); pass_->setObjectName(QStringLiteral("interactionPass"));
    actions->addWidget(confirm_); actions->addWidget(cancel_); actions->addWidget(pass_); actions->addStretch();
    layout->addLayout(actions);
    hide();
}

void InteractionPanel::setContext(const QString& title, const QString& prompt, const QString& detail)
{
    title_->setText(title); prompt_->setText(prompt); detail_->setText(detail); detail_->setVisible(!detail.isEmpty()); show();
}

void InteractionPanel::clearCandidates()
{
    clearLayout(candidates_); candidateCount_ = 0;
}

void InteractionPanel::addCandidate(QWidget* candidate, int row, int column)
{
    candidates_->addWidget(candidate, row, column); ++candidateCount_;
}

void InteractionPanel::setActions(bool confirmVisible, bool confirmEnabled, const QString& confirmText,
                                  std::function<void()> confirm, bool cancelVisible,
                                  std::function<void()> cancel, bool passVisible,
                                  std::function<void()> pass)
{
    confirm_->setVisible(confirmVisible); confirm_->setProperty("ruleEnabled", confirmEnabled); confirm_->setEnabled(confirmEnabled); confirm_->setText(confirmText);
    cancel_->setVisible(cancelVisible); pass_->setVisible(passVisible);
    resetClick(confirm_, std::move(confirm)); resetClick(cancel_, std::move(cancel)); resetClick(pass_, std::move(pass));
}

void InteractionPanel::setAvailable(bool available)
{
    confirm_->setEnabled(available && confirm_->property("ruleEnabled").toBool());
    cancel_->setEnabled(available);
    pass_->setEnabled(available);
}

void InteractionPanel::fitToViewport(const QSize& viewportSize)
{
    const int availableWidth = std::max(1, viewportSize.width() - 24);
    const int availableHeight = std::max(1, viewportSize.height() - 24);
    const int panelWidth = std::min(availableWidth, std::max(320, viewportSize.width() * 82 / 100));
    const int panelHeight = std::min(availableHeight, std::max(190, viewportSize.height() / 2));
    setGeometry((viewportSize.width() - panelWidth) / 2, (viewportSize.height() - panelHeight) / 2,
                panelWidth, panelHeight);
    raise();
}

void InteractionPanel::reset()
{
    clearCandidates(); title_->clear(); prompt_->clear(); detail_->clear();
    setActions(false, false, tr("确认"), {}, false, {}, false, {}); hide();
}

} // namespace sanguosha::ui
