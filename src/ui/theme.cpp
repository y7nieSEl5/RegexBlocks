#include "ui/theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QSettings>

namespace {
constexpr const char *kKeyMode = "theme/mode";

// ============================================================================
// Light 色板
// ============================================================================
struct LightPalette {
    static constexpr const char *canvasBg         = "#fafafa";
    static constexpr const char *placeholderText  = "#9e9e9e";
    static constexpr const char *panelBg          = "#ffffff";
    static constexpr const char *panelAlt         = "#f5f5f5";
    static constexpr const char *border           = "#e0e0e0";
    static constexpr const char *textPrimary      = "#212121";
    static constexpr const char *textMuted        = "#777777";
    static constexpr const char *errorBg          = "#ffebee";
    static constexpr const char *errorFg          = "#c62828";
    static constexpr const char *splitterHandle   = "#e0e0e0";
    static constexpr const char *selectedItemBg   = "#e3f2fd";
    static constexpr const char *categoryHeaderBg = "#e0e0e0";
    static constexpr const char *categoryHeaderFg = "#555555";
    static constexpr const char *menuItemHover    = "#e3f2fd";
    static constexpr const char *statusBarFg      = "#555555";
};

// ============================================================================
// Dark 色板 (VSCode 风格深灰; 比纯黑柔和, 便于长时间使用)
// ============================================================================
struct DarkPalette {
    static constexpr const char *canvasBg         = "#1e1e1e";
    static constexpr const char *placeholderText  = "#7a7a7a";
    static constexpr const char *panelBg          = "#252526";
    static constexpr const char *panelAlt         = "#2d2d30";
    static constexpr const char *border           = "#3c3c3c";
    static constexpr const char *textPrimary      = "#e0e0e0";
    static constexpr const char *textMuted        = "#a0a0a0";
    static constexpr const char *errorBg          = "#4a1f1f";
    static constexpr const char *errorFg          = "#ff8a80";
    static constexpr const char *splitterHandle   = "#3c3c3c";
    static constexpr const char *selectedItemBg   = "#094771";   // VSCode 深蓝选中
    static constexpr const char *categoryHeaderBg = "#333333";
    static constexpr const char *categoryHeaderFg = "#bdbdbd";
    static constexpr const char *menuItemHover    = "#094771";
    static constexpr const char *statusBarFg      = "#a0a0a0";
};

// 品牌按钮色 (两档共用; 在两种背景下都好看)
constexpr const char *kBrandPrimary = "#1e88e5";
constexpr const char *kBrandHover   = "#1976d2";
constexpr const char *kBrandPressed = "#1565c0";

// 高亮 (匹配): 黄色半透明; 暗色降饱和度避免刺眼
const QColor kMatchHiLight = QColor(255, 235, 59, 140);
const QColor kMatchHiDark  = QColor(255, 213,  79,  90);

// ============================================================================
// QSS 模板生成
// ============================================================================
QString buildStyleSheet(Theme::Mode mode) {
    const bool dark = (mode == Theme::Mode::Dark);

#define C(name) (dark ? DarkPalette::name : LightPalette::name)

    return QString(R"(
QStatusBar  { color: %STATUS_FG%; }
QSplitter::handle:horizontal { width: 4px; background: %HANDLE%; }
QSplitter::handle:vertical   { height: 4px; background: %HANDLE%; }
QMenuBar::item:selected { background: %MENU_HOVER%; }
QMenu::item:selected    { background: %MENU_HOVER%; }
QToolTip {
    background: %PANEL_ALT%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    padding: 4px 6px;
}

QPushButton {
    background: %BRAND%;
    color: white;
    border: none;
    padding: 5px 14px;
    border-radius: 4px;
    min-height: 18px;
}
QPushButton:hover     { background: %BRAND_HOVER%; }
QPushButton:pressed   { background: %BRAND_PRESSED%; }
QPushButton:disabled  { background: %BORDER%; color: %TEXT_MUTED%; }
QPushButton:default   { font-weight: bold; }
)")
        .replace("%STATUS_FG%",     C(statusBarFg))
        .replace("%HANDLE%",        C(splitterHandle))
        .replace("%MENU_HOVER%",    C(menuItemHover))
        .replace("%PANEL_ALT%",     C(panelAlt))
        .replace("%TEXT%",          C(textPrimary))
        .replace("%TEXT_MUTED%",    C(textMuted))
        .replace("%BORDER%",        C(border))
        .replace("%BRAND%",         kBrandPrimary)
        .replace("%BRAND_HOVER%",   kBrandHover)
        .replace("%BRAND_PRESSED%", kBrandPressed);

#undef C
}

// ============================================================================
// QPalette 构造
// ============================================================================
QPalette buildPalette(Theme::Mode mode) {
    const bool dark = (mode == Theme::Mode::Dark);

    QPalette p;
    if (dark) {
        const QColor base("#1e1e1e");
        const QColor alt("#252526");
        const QColor text("#e0e0e0");
        const QColor disabled("#6f6f6f");
        const QColor mid("#3c3c3c");
        const QColor highlight("#094771");
        p.setColor(QPalette::Window,          QColor("#252526"));
        p.setColor(QPalette::WindowText,      text);
        p.setColor(QPalette::Base,            base);
        p.setColor(QPalette::AlternateBase,   alt);
        p.setColor(QPalette::Text,            text);
        p.setColor(QPalette::Button,          alt);
        p.setColor(QPalette::ButtonText,      text);
        p.setColor(QPalette::BrightText,      QColor("#ffffff"));
        p.setColor(QPalette::Highlight,       highlight);
        p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        p.setColor(QPalette::Mid,             mid);
        p.setColor(QPalette::Light,           QColor("#3c3c3c"));
        p.setColor(QPalette::Dark,            QColor("#1a1a1a"));
        p.setColor(QPalette::Shadow,          QColor("#000000"));
        p.setColor(QPalette::PlaceholderText, QColor("#7a7a7a"));
        p.setColor(QPalette::ToolTipBase,     alt);
        p.setColor(QPalette::ToolTipText,     text);
        p.setColor(QPalette::Link,            QColor("#4fc3f7"));
        p.setColor(QPalette::LinkVisited,     QColor("#9575cd"));
        p.setColor(QPalette::Disabled, QPalette::Text,       disabled);
        p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
        p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    }
    // Light: 留空, 用 Fusion 默认 palette (跟系统亮色一致)
    return p;
}
} // namespace

// ============================================================================
// Theme 实现
// ============================================================================

Theme &Theme::instance() {
    static Theme s;
    return s;
}

Theme::Theme() = default;

void Theme::applySaved() {
    const QString saved = QSettings().value(kKeyMode, "light").toString();
    const Mode m = (saved == "dark") ? Mode::Dark : Mode::Light;
    apply(m);
    m_mode = m;
}

void Theme::setMode(Mode mode) {
    if (mode == m_mode) return;
    m_mode = mode;
    QSettings().setValue(kKeyMode, mode == Mode::Dark ? "dark" : "light");
    apply(mode);
    emit themeChanged();
}

void Theme::apply(Mode mode) {
    // Fusion 在三平台都支持完整 palette / QSS, 是暗色模式最稳妥的选择.
    // (macOS 默认 style 不会响应 setPalette 的部分键)
    if (auto *fusion = QStyleFactory::create("Fusion")) {
        QApplication::setStyle(fusion);
    }
    if (mode == Mode::Dark) {
        QApplication::setPalette(buildPalette(mode));
    } else if (auto *style = QApplication::style()) {
        // Light: 用当前 style 的标准 palette (跨平台亮色 = 系统熟悉)
        QApplication::setPalette(style->standardPalette());
    }
    qApp->setStyleSheet(buildStyleSheet(mode));
}

// ============================================================================
// 语义颜色 getter
// ============================================================================
#define DEFINE_GETTER(NAME, FIELD) \
    QColor Theme::NAME() const { \
        return QColor(m_mode == Mode::Dark \
                          ? DarkPalette::FIELD \
                          : LightPalette::FIELD); \
    }

DEFINE_GETTER(canvasBg,         canvasBg)
DEFINE_GETTER(placeholderText,  placeholderText)
DEFINE_GETTER(panelBg,          panelBg)
DEFINE_GETTER(panelAlt,         panelAlt)
DEFINE_GETTER(border,           border)
DEFINE_GETTER(textPrimary,      textPrimary)
DEFINE_GETTER(textMuted,        textMuted)
DEFINE_GETTER(errorBg,          errorBg)
DEFINE_GETTER(errorFg,          errorFg)
DEFINE_GETTER(splitterHandle,   splitterHandle)
DEFINE_GETTER(selectedItemBg,   selectedItemBg)
DEFINE_GETTER(categoryHeaderBg, categoryHeaderBg)
DEFINE_GETTER(categoryHeaderFg, categoryHeaderFg)

#undef DEFINE_GETTER

QColor Theme::matchHighlight() const {
    return isDark() ? kMatchHiDark : kMatchHiLight;
}
