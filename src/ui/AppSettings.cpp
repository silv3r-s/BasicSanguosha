#include "ui/AppSettings.h"

#include <QApplication>
#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>

namespace sanguosha::ui {
namespace {
ThemeMode currentTheme = ThemeMode::System;

bool darkTheme()
{
    if (currentTheme == ThemeMode::Dark) return true;
    if (currentTheme == ThemeMode::Light) return false;
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QString styleSheet(bool dark)
{
    const QString window = dark ? "#17191d" : "#e8e1d5";
    const QString panel = dark ? "#24272d" : "#f7f4ee";
    const QString raised = dark ? "#30343b" : "#fffaf0";
    const QString text = dark ? "#f2eee7" : "#33271c";
    const QString muted = dark ? "#c4beb4" : "#625447";
    const QString border = dark ? "#68625a" : "#b7aa98";
    const QString hover = dark ? "#45403a" : "#fff1cf";
    const QString selected = dark ? "#684126" : "#ffe1a6";
    const QString accent = dark ? "#f0a15a" : "#a83226";
    const QString disabled = dark ? "#34373c" : "#e5e3df";
    const QString disabledText = dark ? "#888b90" : "#888888";
    const QString chain = dark ? "#213f49" : "#dff4f6";
    const QString dead = dark ? "#292a2d" : "#ddd9d3";
    const QString dying = dark ? "#512e31" : "#ffe1df";
    const QString actionTitle = dark ? "#fff1d8" : "#35261d";
    const QString source = dark ? "#ffd15c" : "#8a5900";
    const QString target = dark ? "#66b8ff" : "#165c9c";
    const QString cardName = dark ? "#ff716c" : "#9d201d";
    return QStringLiteral(R"(
        QWidget { background:%1; color:%4; selection-background-color:%7; selection-color:%4; }
        QMainWindow, QDialog { background:%1; }
        QFrame, QGroupBox, QScrollArea, QScrollArea > QWidget > QWidget { background:%2; color:%4; }
        QLabel { background:transparent; color:%4; font-size:13px; }
        QLabel#interactionDetail { color:%5; }
        QPushButton, QComboBox { background:%3; color:%4; border:1px solid %6; border-radius:6px; min-height:30px; padding:4px 10px; }
        QPushButton:hover, QComboBox:hover { background:%8; border-color:%9; }
        QPushButton:checked, QPushButton[selected="true"] { background:%7; border:2px solid %9; }
        QPushButton:disabled, QComboBox:disabled { background:%10; color:%11; border-color:%6; }
        QLineEdit, QSpinBox, QPlainTextEdit, QTextEdit, QListView { background:%3; color:%4; border:1px solid %6; border-radius:6px; padding:5px; selection-background-color:%9; }
        QToolTip { background:%3; color:%4; border:1px solid %9; }
        QFrame#interactionPanel, QFrame#actionStage { background:%3; border:2px solid %9; border-radius:10px; }
        QFrame#actionStage[critical="true"] { background:%7; border-color:%9; }
        QLabel#interactionTitle { color:%9; font-size:18px; font-weight:700; }
        QLabel#actionStageTitle { color:%15; font-size:24px; font-weight:800; }
        QLabel#actionSource { color:%16; background:%2; border:2px solid %16; border-radius:9px; padding:12px; font-size:18px; font-weight:800; }
        QLabel#actionTarget { color:%17; background:%2; border:2px solid %17; border-radius:9px; padding:12px; font-size:18px; font-weight:800; }
        QLabel#displayCard { color:%18; background:%3; border:3px solid %9; border-radius:12px; padding:12px; font-size:22px; font-weight:900; }
        QLabel#actionStagePrompt { color:%15; background:%2; border-radius:7px; padding:7px; font-size:15px; font-weight:700; }
        QLabel#compactLogTitle { color:%15; font-size:16px; font-weight:800; }
        QLabel#phaseBanner, QLabel#judgmentResult, QLabel#equipmentEffect, QLabel#instruction { background:%3; border:1px solid %6; border-radius:7px; padding:7px; }
        QLabel#actionBanner { background:%4; color:%3; border-radius:8px; padding:9px; font-size:16px; font-weight:700; }
        QPushButton#primaryAction, QPushButton#interactionConfirm { background:%9; color:white; font-weight:700; }
        QPushButton#handCard, QPushButton#compactTargetCard, QPushButton#optionTile { background:%3; color:%4; border:2px solid %6; border-radius:9px; }
        QPushButton#handCard:hover, QPushButton#compactTargetCard:hover, QPushButton#optionTile:hover { background:%8; border-color:%9; }
        QPushButton#handCard:checked, QPushButton#compactTargetCard:checked, QPushButton#optionTile:checked { background:%7; border:3px solid %9; }
        QFrame#playerPanel { background:%2; border:1px solid %6; border-radius:10px; }
        QFrame#playerPanel[selfPlayer="true"] { border:2px solid #4f78a8; }
        QFrame#playerPanel[currentPlayer="true"] { border:3px solid #d28b18; }
        QFrame#playerPanel[chained="true"] { background:%12; }
        QFrame#playerPanel[alive="false"] { background:%13; color:%11; border-style:dashed; }
        QFrame#playerPanel[dying="true"] { background:%14; border:3px solid #d34b45; }
        QFrame#playerPanel[selectable="true"] { border:2px solid #d28b18; }
        QFrame#playerPanel[selected="true"] { background:%7; border:3px solid %9; }
        QLabel#chainBadge { background:#247b87; color:white; border-radius:5px; padding:2px 6px; font-weight:800; }
        QLabel#portraitSlot { background:%3; color:%9; border:1px solid %6; border-radius:7px; font-size:24px; font-weight:800; }
    )").arg(window, panel, raised, text, muted, border, selected, hover, accent, disabled, disabledText, chain, dead, dying, actionTitle, source, target, cardName);
}
}

ThemeMode AppSettings::readTheme(QSettings& s)
{
    const int value = s.value(QStringLiteral("appearance/theme"), int(ThemeMode::System)).toInt();
    return value >= int(ThemeMode::System) && value <= int(ThemeMode::Dark) ? ThemeMode(value) : ThemeMode::System;
}
ai::AISpeedPreset AppSettings::readAISpeed(QSettings& s)
{
    const int value = s.value(QStringLiteral("gameplay/aiSpeed"), int(ai::AISpeedPreset::Standard)).toInt();
    return value >= int(ai::AISpeedPreset::Test) && value <= int(ai::AISpeedPreset::Slow) ? ai::AISpeedPreset(value) : ai::AISpeedPreset::Standard;
}
void AppSettings::writeTheme(QSettings& s, ThemeMode mode) { s.setValue(QStringLiteral("appearance/theme"), int(mode)); }
void AppSettings::writeAISpeed(QSettings& s, ai::AISpeedPreset preset) { s.setValue(QStringLiteral("gameplay/aiSpeed"), int(preset)); }

void AppSettings::initialize()
{
    QSettings settings;
    currentTheme = readTheme(settings);
    ai::AIActionPacing::setPreset(readAISpeed(settings));
    applyTheme();
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, [](Qt::ColorScheme) {
        if (currentTheme == ThemeMode::System) AppSettings::applyTheme();
    });
}
ThemeMode AppSettings::themeMode() { return currentTheme; }
void AppSettings::setThemeMode(ThemeMode mode) { currentTheme = mode; QSettings s; writeTheme(s, mode); applyTheme(); }
void AppSettings::applyThemeForTesting(ThemeMode mode) { currentTheme = mode; applyTheme(); }
ai::AISpeedPreset AppSettings::aiSpeedPreset() { return ai::AIActionPacing::preset(); }
void AppSettings::setAISpeedPreset(ai::AISpeedPreset preset) { ai::AIActionPacing::setPreset(preset); QSettings s; writeAISpeed(s, preset); }
void AppSettings::applyTheme() { if (qApp) qApp->setStyleSheet(styleSheet(darkTheme())); }
QString AppSettings::themeLabel(ThemeMode mode) { return mode == ThemeMode::System ? QStringLiteral("跟随系统") : mode == ThemeMode::Light ? QStringLiteral("浅色") : QStringLiteral("深色"); }
QString AppSettings::aiSpeedLabel(ai::AISpeedPreset preset) { return preset == ai::AISpeedPreset::Test ? QStringLiteral("测试") : preset == ai::AISpeedPreset::Fast ? QStringLiteral("快速") : preset == ai::AISpeedPreset::Slow ? QStringLiteral("慢速") : QStringLiteral("标准"); }

} // namespace sanguosha::ui
