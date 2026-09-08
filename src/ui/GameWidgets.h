#pragma once

#include <QFrame>
#include <QPushButton>

#include <functional>

#include "session/PlayerViewState.h"

class QLabel;
class QMouseEvent;

namespace sanguosha::ui {

class CardButton final : public QPushButton {
public:
    explicit CardButton(const CardView& card, QWidget* parent = nullptr);
    void setSelected(bool selected);
};

class CompactTargetCard final : public QPushButton {
public:
    explicit CompactTargetCard(const PublicPlayerView& player, bool identityMode, bool selected, QWidget* parent = nullptr);
    void setSelected(bool selected);
};

class PlayerPanel final : public QFrame {
public:
    explicit PlayerPanel(QWidget* parent = nullptr);
    void setPlayer(const PublicPlayerView& player, bool identityMode, bool self, bool current);
    void setSelectable(bool selectable, bool selected, std::function<void()> activate = {});

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void updateStyle();
    QLabel* title_ {};
    QLabel* portrait_ {};
    QLabel* identity_ {};
    QLabel* hp_ {};
    QLabel* state_ {};
    QLabel* chainBadge_ {};
    QLabel* equipment_ {};
    QLabel* judgment_ {};
    bool self_ {false};
    bool current_ {false};
    bool selectable_ {false};
    bool selected_ {false};
    std::function<void()> activate_;
};

} // namespace sanguosha::ui
