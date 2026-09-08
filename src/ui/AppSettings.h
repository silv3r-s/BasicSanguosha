#pragma once

#include <QString>

#include "ai/AIActionPacing.h"

class QSettings;

namespace sanguosha::ui {

enum class ThemeMode { System, Light, Dark };

class AppSettings final {
public:
    static void initialize();
    [[nodiscard]] static ThemeMode themeMode();
    static void setThemeMode(ThemeMode mode);
    static void applyThemeForTesting(ThemeMode mode);
    [[nodiscard]] static ai::AISpeedPreset aiSpeedPreset();
    static void setAISpeedPreset(ai::AISpeedPreset preset);
    [[nodiscard]] static ThemeMode readTheme(QSettings& settings);
    [[nodiscard]] static ai::AISpeedPreset readAISpeed(QSettings& settings);
    static void writeTheme(QSettings& settings, ThemeMode mode);
    static void writeAISpeed(QSettings& settings, ai::AISpeedPreset preset);
    [[nodiscard]] static QString themeLabel(ThemeMode mode);
    [[nodiscard]] static QString aiSpeedLabel(ai::AISpeedPreset preset);

private:
    static void applyTheme();
};

} // namespace sanguosha::ui
