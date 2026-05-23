#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>

class QLineEdit;
class QPlainTextEdit;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QTimer;
class QCheckBox;
class QPushButton;

// 右侧测试面板:
//   - 显示当前正则
//   - 用户输入测试文本
//   - 实时高亮所有匹配, 列出每个匹配 (含分组捕获)
//   - 点击匹配项 → 编辑器自动选中并滚动
//   - 替换模式: 输入替换字符串, 一键替换全部
class TestPanel : public QWidget {
    Q_OBJECT
public:
    explicit TestPanel(QWidget *parent = nullptr);

    QString testText() const;
    void    setTestText(const QString &text);

    // 正则 flags 序列化 (跨平台 JSON)
    //   "i" = 忽略大小写, "g" = 全局匹配, "m" = 多行模式,
    //   "u" = Unicode 属性, "y" = sticky(从当前位置锚定), "s" = . 匹配换行
    QStringList flags() const;
    void        setFlags(const QStringList &flags);

    // 替换字符串 (用于序列化保存)
    QString replacement() const;
    void    setReplacement(const QString &s);

public slots:
    // 由 RegexCompiler 调用: 设置当前要测试的正则 (走完整慢路径: 跑匹配 + 更新错误样式)
    void setRegex(const QString &regex, bool valid = true, const QString &warning = {});
    // 拖动 60Hz 期间调用: 仅刷新文本框, 不跑匹配 (避免高频 QRegularExpression)
    void setRegexPreview(const QString &regex);

private slots:
    void scheduleMatch();
    void runMatch();
    void onMatchItemClicked(QListWidgetItem *item);
    void onReplaceAllClicked();
    void onCopyMenuRequested();

private:
    QLineEdit      *m_regexDisplay   = nullptr;
    QPlainTextEdit *m_testText       = nullptr;
    QListWidget    *m_matchList      = nullptr;
    QLabel         *m_statusLabel    = nullptr;
    QLabel         *m_matchCountLbl  = nullptr;
    QTimer         *m_debounce       = nullptr;

    // 正则 flags 复选框
    QCheckBox      *m_flagCaseI      = nullptr;  // i - 忽略大小写
    QCheckBox      *m_flagGlobalG     = nullptr;  // g - 全局匹配
    QCheckBox      *m_flagMultiLine  = nullptr;  // m - 多行 ^$
    QCheckBox      *m_flagUnicodeU    = nullptr;  // u - Unicode 属性
    QCheckBox      *m_flagStickyY     = nullptr;  // y - sticky
    QCheckBox      *m_flagDotAll     = nullptr;  // s - . 匹配换行

    // 替换模式
    QLineEdit      *m_replaceEdit    = nullptr;
    QPushButton    *m_replaceAllBtn  = nullptr;

    QString m_currentRegex;
    bool    m_currentValid = true;

signals:
    void matchCountChanged(int count);            // 通知 MainWindow 在状态栏更新
    void dirtied();                                // 用户改了测试文本 / flags / 替换内容 → MainWindow 标脏
    void regexEditedByUser(const QString &regex);  // 用户在右上角输入框 Enter / 失焦后 → 触发反向解析
    void snippetCopied(const QString &lang);       // 状态栏提示用
};
