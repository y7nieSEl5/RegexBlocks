#pragma once

#include <QObject>
#include <QColor>
#include <QString>

// 应用级主题管理 (Light / Dark 两档).
//   - 单例 Theme::instance()
//   - apply(mode) 一次性更新 QApplication palette + 全局 QSS + style("Fusion")
//   - 提供语义颜色 getter, 供"自己用 painter 画"的组件 (画布/积木/匹配高亮) 使用
//   - 切换时发 themeChanged() 信号; 监听者只需要 update() 即可立即重绘
//   - 模式持久化到 QSettings 的 "theme/mode" 键
class Theme : public QObject {
    Q_OBJECT
public:
    enum class Mode { Light, Dark };
    Q_ENUM(Mode)

    static Theme &instance();

    Mode mode() const { return m_mode; }

    // 写设置 + 应用 + 发 themeChanged
    void setMode(Mode mode);

    // 从 QSettings 读 "theme/mode" (默认 Light), 然后 apply
    // 在 main.cpp QApplication 构造之后、MainWindow 之前调用一次
    void applySaved();

    // ---- 语义颜色 getter (paint 代码使用) -----------------------------------
    QColor canvasBg()       const;   // BlockCanvas 视图背景
    QColor placeholderText() const;  // 画布"把积木拖到这里"提示
    QColor panelBg()        const;   // 通用面板底色
    QColor panelAlt()       const;   // 分类条 / 次要表面
    QColor border()         const;   // 通用描边色
    QColor textPrimary()    const;
    QColor textMuted()      const;   // 提示性灰文字
    QColor errorBg()        const;   // 正则非法时输入框背景
    QColor errorFg()        const;   // 错误前景色
    QColor matchHighlight() const;   // QTextEdit 匹配高亮 (半透明)
    QColor splitterHandle() const;
    QColor selectedItemBg() const;   // 列表选中底色
    QColor categoryHeaderBg() const; // 积木库分类条
    QColor categoryHeaderFg() const;

    // 是否暗色 (供需要二选一逻辑时用, 例如选不同图标)
    bool isDark() const { return m_mode == Mode::Dark; }

signals:
    void themeChanged();

private:
    Theme();
    Q_DISABLE_COPY_MOVE(Theme)

    void apply(Mode mode);

    Mode m_mode = Mode::Light;
};
