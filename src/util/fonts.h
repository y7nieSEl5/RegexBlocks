#pragma once

// 跨平台字体抽象 - 让 Qt 自动选择各平台最佳字体
//   macOS:   SF Pro Text / SF Mono
//   Windows: Segoe UI / Cascadia Mono / Consolas
//   Linux:   系统默认 sans / DejaVu Sans Mono
//
// 永远不要在源码里硬编码字体名 ("Helvetica" / "Menlo" 等),
// 否则在非 macOS 系统会回退成 Qt 默认字体, 视觉效果不一致。

#include <QApplication>
#include <QFont>
#include <QFontDatabase>

namespace ui {

// 通用 UI 字体 (用于积木上的文字、列表、对话框等)
inline QFont uiFont(int pointSize = 13, bool bold = false) {
    QFont f = QApplication::font();
    f.setPointSize(pointSize);
    f.setBold(bold);
    return f;
}

// 等宽字体 (用于显示正则字符串等代码风格内容)
inline QFont monoFont(int pointSize = 12) {
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(pointSize);
    f.setStyleHint(QFont::Monospace);
    return f;
}

} // namespace ui
