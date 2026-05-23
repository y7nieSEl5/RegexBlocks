#include "ui/testpanel.h"

#include "ui/theme.h"
#include "util/fonts.h"

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QAction>
#include <QCheckBox>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QGuiApplication>
#include <QClipboard>

namespace {
// 代码片段生成 helper

QString jsFlags(bool i, bool g, bool m, bool u, bool y, bool s) {
    QString f;
    if (i) f += 'i';
    if (g) f += 'g';
    if (m) f += 'm';
    if (u) f += 'u';
    if (y) f += 'y';
    if (s) f += 's';
    return f;
}

QString pythonFlags(bool i, bool m, bool u, bool s) {
    QStringList parts;
    if (i) parts << QStringLiteral("re.I");
    if (m) parts << QStringLiteral("re.M");
    if (u) parts << QStringLiteral("re.UNICODE");
    if (s) parts << QStringLiteral("re.S");
    return parts.join(QStringLiteral(" | "));
}

QString javaFlags(bool i, bool m, bool u, bool s) {
    QStringList parts;
    if (i) parts << QStringLiteral("Pattern.CASE_INSENSITIVE");
    if (m) parts << QStringLiteral("Pattern.MULTILINE");
    if (u) parts << QStringLiteral("Pattern.UNICODE_CHARACTER_CLASS");
    if (s) parts << QStringLiteral("Pattern.DOTALL");
    return parts.join(QStringLiteral(" | "));
}

QString rustInlineFlags(bool i, bool m, bool u, bool s) {
    QString f;
    if (i) f += 'i';
    if (m) f += 'm';
    if (u) f += 'u';
    if (s) f += 's';
    return f.isEmpty() ? QString() : QStringLiteral("(?%1)").arg(f);
}

QString cppQtFlags(bool i, bool m, bool u, bool s) {
    QStringList parts;
    if (i) parts << QStringLiteral("QRegularExpression::CaseInsensitiveOption");
    if (m) parts << QStringLiteral("QRegularExpression::MultilineOption");
    if (u) parts << QStringLiteral("QRegularExpression::UseUnicodePropertiesOption");
    if (s) parts << QStringLiteral("QRegularExpression::DotMatchesEverythingOption");
    if (parts.isEmpty()) return QStringLiteral("QRegularExpression::NoPatternOption");
    return parts.join(QStringLiteral(" | "));
}

// 转义 Java / 双引号字符串中的字符
QString escapeForJavaString(const QString &s) {
    QString out;
    out.reserve(s.size() + 4);
    for (QChar c : s) {
        if (c == QChar('\\') || c == QChar('"')) out.append('\\');
        out.append(c);
    }
    return out;
}

// 生成 5 门语言代码片段
QString snippetPCRE(const QString &regex, bool i, bool, bool m, bool u, bool, bool s) {
    QString inlineFlags;
    if (i) inlineFlags += QChar('i');
    if (m) inlineFlags += QChar('m');
    if (u) inlineFlags += QChar('u');
    if (s) inlineFlags += QChar('s');
    if (inlineFlags.isEmpty()) return regex;
    return QStringLiteral("(?%1)%2").arg(inlineFlags, regex);
}

QString snippetPython(const QString &regex, bool i, bool g, bool m, bool u, bool y, bool s) {
    const QString flags = pythonFlags(i, m, u, s);
    QString header = QStringLiteral("import re\n");
    if (y) {
        header += QStringLiteral("# Python re 没有与 JS y 完全等价的 sticky 语义, 这里按常规匹配示例\n");
    }
    if (g) {
        if (flags.isEmpty()) {
            return header + QStringLiteral("list(re.finditer(r\"%1\", text))").arg(regex);
        }
        return header + QStringLiteral("list(re.finditer(r\"%1\", text, flags=%2))")
                            .arg(regex, flags);
    }
    if (flags.isEmpty()) {
        return header + QStringLiteral("re.search(r\"%1\", text)").arg(regex);
    }
    return header + QStringLiteral("re.search(r\"%1\", text, flags=%2)").arg(regex, flags);
}

QString snippetJS(const QString &regex, bool i, bool g, bool m, bool u, bool y, bool s) {
    const QString flags = jsFlags(i, g, m, u, y, s);
    if (g) {
        return QStringLiteral("const re = /%1/%2;\n"
                              "[...text.matchAll(re)]").arg(regex, flags);
    }
    return QStringLiteral("const re = /%1/%2;\n"
                          "re.exec(text)").arg(regex, flags);
}

QString snippetJava(const QString &regex, bool i, bool g, bool m, bool u, bool y, bool s) {
    Q_UNUSED(g)
    Q_UNUSED(y)
    const QString esc = escapeForJavaString(regex);
    const QString flags = javaFlags(i, m, u, s);
    if (flags.isEmpty()) {
        return QStringLiteral("Pattern.compile(\"%1\")\n"
                              "    .matcher(text)\n"
                              "    .results()").arg(esc);
    }
    return QStringLiteral("Pattern.compile(\"%1\", %2)\n"
                          "    .matcher(text)\n"
                          "    .results()").arg(esc, flags);
}

QString snippetRust(const QString &regex, bool i, bool g, bool m, bool u, bool y, bool s) {
    Q_UNUSED(g)
    Q_UNUSED(y)
    const QString prefix = rustInlineFlags(i, m, u, s);
    const QString full = prefix + regex;
    return QStringLiteral(
        "// Cargo.toml: fancy-regex = \"0.18\"\n"
        "use fancy_regex::Regex;\n"
        "\n"
        "let re = Regex::new(r\"%1\").unwrap();\n"
        "for m in re.find_iter(text) {\n"
        "    let m = m.unwrap();\n"
        "    println!(\"{}\", m.as_str());\n"
        "}").arg(full);
}

QString snippetCppQt(const QString &regex, bool i, bool g, bool m, bool u, bool y, bool s) {
    Q_UNUSED(g)
    Q_UNUSED(y)
    const QString esc = escapeForJavaString(regex);
    const QString opts = cppQtFlags(i, m, u, s);
    return QStringLiteral(
        "#include <QRegularExpression>\n"
        "\n"
        "QRegularExpression re(QStringLiteral(\"%1\"), %2);\n"
        "QRegularExpressionMatchIterator it = re.globalMatch(text);\n"
        "while (it.hasNext()) {\n"
        "    const QRegularExpressionMatch m = it.next();\n"
        "    qDebug() << m.captured();\n"
        "}").arg(esc, opts);
}

} // namespace

namespace {
constexpr int kDebounceMs = 200;
// QListWidgetItem custom data roles for jump-to-match
constexpr int kRoleStart = Qt::UserRole + 1;
constexpr int kRoleEnd   = Qt::UserRole + 2;

QString errorStyleSheet() {
    auto &t = Theme::instance();
    return QString("QLineEdit { background: %1; color: %2; }")
        .arg(t.errorBg().name(), t.errorFg().name());
}

QString mutedStatusStyleSheet() {
    return QString("color: %1; font-size: 11px;")
        .arg(Theme::instance().errorFg().name());
}
} // namespace

TestPanel::TestPanel(QWidget *parent)
    : QWidget(parent) {
    setMinimumWidth(340);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    layout->addWidget(new QLabel(tr("生成的正则表达式 (可直接编辑反向导入):")));

    auto *regexRow = new QHBoxLayout;
    m_regexDisplay = new QLineEdit;
    m_regexDisplay->setReadOnly(false);   // 可编辑: 用户输入 / Enter / 失焦后反向解析
    m_regexDisplay->setPlaceholderText(tr("输入正则或拖拽积木生成"));
    m_regexDisplay->setFont(ui::monoFont(12));
    m_regexDisplay->setToolTip(tr(
        "可以直接在这里输入正则字符串.\n"
        "按 Enter 或失焦后, 工具会尝试反向解析回积木树并自动布局到画布."));

    // "复制 ▾" 下拉菜单按钮 (5 门语言代码片段)
    auto *copyBtn = new QToolButton;
    copyBtn->setText(tr("复制 ▾"));
    copyBtn->setPopupMode(QToolButton::InstantPopup);
    copyBtn->setToolTip(tr("把当前正则按指定语言的代码片段复制到剪贴板"));
    auto *copyMenu = new QMenu(copyBtn);

    auto addSnippetAction = [&](const QString &label,
                                const QString &lang,
                                const QString &tip,
                                std::function<QString(const QString&, bool, bool, bool, bool, bool, bool)> gen) {
        auto *act = copyMenu->addAction(label);
        if (!tip.isEmpty()) act->setToolTip(tip);
        connect(act, &QAction::triggered, this, [this, lang, gen]() {
            const bool i = m_flagCaseI->isChecked();
            const bool g = m_flagGlobalG->isChecked();
            const bool m = m_flagMultiLine->isChecked();
            const bool u = m_flagUnicodeU->isChecked();
            const bool y = m_flagStickyY->isChecked();
            const bool s = m_flagDotAll->isChecked();
            const QString snippet = gen(m_currentRegex, i, g, m, u, y, s);
            QGuiApplication::clipboard()->setText(snippet);
            emit snippetCopied(lang);
        });
    };
    addSnippetAction(tr("PCRE / 原始字符串"), tr("PCRE"),
                     tr("直接复制正则字符串 (Qt QRegularExpression 即 PCRE2 兼容)"),
                     snippetPCRE);
    addSnippetAction(tr("C++ (Qt / QRegularExpression)"), tr("C++"),
                     tr("QRegularExpression + globalMatch 示例代码"), snippetCppQt);
    addSnippetAction(tr("Python (re)"), tr("Python"),
                     tr("import re; re.findall(...)"), snippetPython);
    addSnippetAction(tr("JavaScript"), tr("JavaScript"),
                     tr("const re = /.../;  text.matchAll(re)"), snippetJS);
    addSnippetAction(tr("Java (Pattern)"), tr("Java"),
                     tr("Pattern.compile(...).matcher(text).results()"), snippetJava);
    auto *rustAct = copyMenu->addAction(tr("Rust (fancy-regex)"));
    rustAct->setToolTip(tr(
        "fancy-regex 在底层 regex crate 之上加了回溯 VM,\n"
        "支持反向引用 \\1 / \\k<name> 等本工具会生成的特性;\n"
        "纯 NFA 子集会被自动委派回 regex 引擎, 性能基本无损."));
    connect(rustAct, &QAction::triggered, this, [this]() {
        const bool i = m_flagCaseI->isChecked();
        const bool g = m_flagGlobalG->isChecked();
        const bool m = m_flagMultiLine->isChecked();
        const bool u = m_flagUnicodeU->isChecked();
        const bool y = m_flagStickyY->isChecked();
        const bool s = m_flagDotAll->isChecked();
        QGuiApplication::clipboard()->setText(snippetRust(m_currentRegex, i, g, m, u, y, s));
        emit snippetCopied(tr("Rust"));
    });
    copyBtn->setMenu(copyMenu);

    regexRow->addWidget(m_regexDisplay, 1);
    regexRow->addWidget(copyBtn);
    layout->addLayout(regexRow);

    // ---- 正则 flags 行 ----
    auto *flagsRow = new QHBoxLayout;
    flagsRow->setSpacing(10);
    auto *flagsTitle = new QLabel(tr("Flags (i/g/m/u/y/s):"));
    QFont flagsTitleFont = flagsTitle->font();
    flagsTitleFont.setBold(true);
    flagsTitle->setFont(flagsTitleFont);
    flagsRow->addWidget(flagsTitle);
    m_flagCaseI = new QCheckBox(tr("i 忽略大小写"));
    m_flagCaseI->setToolTip(tr("i - CaseInsensitive"));
    m_flagGlobalG = new QCheckBox(tr("g 全局"));
    m_flagGlobalG->setToolTip(tr("g - Global: 勾选时列出全部匹配; 取消后仅取首个匹配"));
    m_flagGlobalG->setChecked(true);
    m_flagMultiLine = new QCheckBox(tr("m 多行"));
    m_flagMultiLine->setToolTip(tr("m - MultiLine: ^ 和 $ 匹配每行而不是整个字符串"));
    m_flagUnicodeU = new QCheckBox(tr("u Unicode"));
    m_flagUnicodeU->setToolTip(tr("u - UseUnicodeProperties: \\w / \\d / \\b 等按 Unicode 语义处理"));
    m_flagStickyY = new QCheckBox(tr("y sticky"));
    m_flagStickyY->setToolTip(tr("y - Sticky: 从当前位置锚定匹配, 一旦中断即停止"));
    m_flagDotAll = new QCheckBox(tr("s dotAll"));
    m_flagDotAll->setToolTip(tr("s - DotMatchesEverything: . 也能匹配 \\n"));
    flagsRow->addWidget(m_flagCaseI);
    flagsRow->addWidget(m_flagGlobalG);
    flagsRow->addWidget(m_flagMultiLine);
    flagsRow->addWidget(m_flagUnicodeU);
    flagsRow->addWidget(m_flagStickyY);
    flagsRow->addWidget(m_flagDotAll);
    flagsRow->addStretch(1);
    layout->addLayout(flagsRow);

    // ---- 替换模式行 ----
    auto *replaceRow = new QHBoxLayout;
    replaceRow->addWidget(new QLabel(tr("替换为:")));
    m_replaceEdit = new QLineEdit;
    m_replaceEdit->setPlaceholderText(tr("输入替换内容 (\\0 = 整段匹配, \\1 \\2 = 捕获组), 留空可清除匹配"));
    m_replaceEdit->setFont(ui::monoFont(12));
    m_replaceAllBtn = new QPushButton(tr("替换全部"));
    m_replaceAllBtn->setToolTip(tr(
        "用替换字符串替换测试文本中所有匹配; ⌘Z / Ctrl+Z 可撤销.\n"
        "反向引用使用 Qt 语法: \\0 = 整段匹配, \\1..\\99 = 第 N 个捕获组.\n"
        "不支持 JavaScript 风格的 $0 $1 $& $` $' (会原样输出);\n"
        "不支持 \\n \\t 转义 (会原样输出 backslash + 字母)."));
    replaceRow->addWidget(m_replaceEdit, 1);
    replaceRow->addWidget(m_replaceAllBtn);
    layout->addLayout(replaceRow);

    m_statusLabel = new QLabel;
    m_statusLabel->setStyleSheet(mutedStatusStyleSheet());
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();
    layout->addWidget(m_statusLabel);

    layout->addSpacing(4);
    layout->addWidget(new QLabel(tr("测试文本:")));
    m_testText = new QPlainTextEdit;
    m_testText->setPlaceholderText(tr("在这里粘贴或输入要测试的文本..."));
    m_testText->setPlainText(tr(
        "示例文本:\n"
        "联系电话 138-2345 或 010-1111\n"
        "邮箱: hello@world.com 和 user@example.org\n"
        "年份 2023, 2024-05-02"));
    layout->addWidget(m_testText, 2);

    m_matchCountLbl = new QLabel(tr("匹配结果: (点击列表项可在文本中跳转到对应位置)"));
    layout->addWidget(m_matchCountLbl);
    m_matchList = new QListWidget;
    m_matchList->setToolTip(tr("点击任一匹配项 → 编辑器自动选中并滚动到该位置"));
    layout->addWidget(m_matchList, 1);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &TestPanel::runMatch);

    // 用户编辑右上角输入框: 仅 Enter / 失焦后触发反向解析 (避免边打边失败)
    connect(m_regexDisplay, &QLineEdit::editingFinished, this, [this]() {
        const QString txt = m_regexDisplay->text();
        if (txt == m_currentRegex) return;
        emit regexEditedByUser(txt);
    });

    connect(m_testText, &QPlainTextEdit::textChanged, this, [this]() {
        scheduleMatch();
        emit dirtied();
    });

    // flags 变化也要重新匹配 + 标脏
    for (auto *cb : {m_flagCaseI, m_flagGlobalG, m_flagMultiLine,
                     m_flagUnicodeU, m_flagStickyY, m_flagDotAll}) {
        connect(cb, &QCheckBox::toggled, this, [this]() {
            scheduleMatch();
            emit dirtied();
        });
    }

    // 替换
    connect(m_replaceEdit, &QLineEdit::textChanged, this, [this]() { emit dirtied(); });
    connect(m_replaceAllBtn, &QPushButton::clicked,
            this, &TestPanel::onReplaceAllClicked);

    // 匹配项点击 → 编辑器跳转
    connect(m_matchList, &QListWidget::itemClicked,
            this, &TestPanel::onMatchItemClicked);

    // 主题切换 → 重设错误样式 / 重新跑一次匹配 (高亮颜色随之更新)
    connect(&Theme::instance(), &Theme::themeChanged, this, [this]() {
        m_statusLabel->setStyleSheet(mutedStatusStyleSheet());
        // 如果当前正则错误, 错误背景色需要刷新
        if (!m_currentValid && !m_currentRegex.isEmpty()) {
            m_regexDisplay->setStyleSheet(errorStyleSheet());
        } else {
            m_regexDisplay->setStyleSheet("");
        }
        scheduleMatch();   // 重新计算 ExtraSelection (用新主题的高亮色)
    });
}

QStringList TestPanel::flags() const {
    QStringList out;
    if (m_flagCaseI->isChecked())     out << "i";
    if (m_flagGlobalG->isChecked())   out << "g";
    if (m_flagMultiLine->isChecked()) out << "m";
    if (m_flagUnicodeU->isChecked())  out << "u";
    if (m_flagStickyY->isChecked())   out << "y";
    if (m_flagDotAll->isChecked())    out << "s";
    return out;
}

void TestPanel::setFlags(const QStringList &flags) {
    // 临时阻断信号, 加载文件时不应触发 dirtied()
    for (auto *cb : {m_flagCaseI, m_flagGlobalG, m_flagMultiLine,
                     m_flagUnicodeU, m_flagStickyY, m_flagDotAll}) {
        cb->blockSignals(true);
    }
    m_flagCaseI->setChecked(flags.contains("i"));
    m_flagGlobalG->setChecked(flags.contains("g"));
    m_flagMultiLine->setChecked(flags.contains("m"));
    m_flagUnicodeU->setChecked(flags.contains("u"));
    m_flagStickyY->setChecked(flags.contains("y"));
    m_flagDotAll->setChecked(flags.contains("s"));
    for (auto *cb : {m_flagCaseI, m_flagGlobalG, m_flagMultiLine,
                     m_flagUnicodeU, m_flagStickyY, m_flagDotAll}) {
        cb->blockSignals(false);
    }
    scheduleMatch();
}

QString TestPanel::testText() const {
    return m_testText->toPlainText();
}

void TestPanel::setTestText(const QString &text) {
    m_testText->blockSignals(true);
    m_testText->setPlainText(text);
    m_testText->blockSignals(false);
    scheduleMatch();
}

QString TestPanel::replacement() const {
    return m_replaceEdit->text();
}

void TestPanel::setReplacement(const QString &s) {
    m_replaceEdit->blockSignals(true);
    m_replaceEdit->setText(s);
    m_replaceEdit->blockSignals(false);
}

void TestPanel::setRegex(const QString &regex, bool valid, const QString &warning) {
    m_currentRegex = regex;
    m_currentValid = valid;
    // blockSignals 防止 setText 触发 editingFinished 回环
    m_regexDisplay->blockSignals(true);
    m_regexDisplay->setText(regex);
    m_regexDisplay->blockSignals(false);

    if (!valid) {
        m_regexDisplay->setStyleSheet(errorStyleSheet());
    } else {
        m_regexDisplay->setStyleSheet("");
    }
    if (!warning.isEmpty()) {
        m_statusLabel->setText(warning);
        m_statusLabel->show();
    } else {
        m_statusLabel->hide();
        m_statusLabel->clear();
    }

    scheduleMatch();
}

void TestPanel::setRegexPreview(const QString &regex) {
    // 拖动 60Hz 期间调用: 仅刷新文本, 不重置 valid, 不调 scheduleMatch.
    // 也不更新 m_currentRegex (避免 editingFinished 时 == m_currentRegex 比较失效).
    // 真正的"提交"路径走 setRegex().
    if (m_regexDisplay->text() == regex) return;
    m_regexDisplay->blockSignals(true);
    m_regexDisplay->setText(regex);
    m_regexDisplay->blockSignals(false);
}

void TestPanel::onCopyMenuRequested() {
    // 占位: 当前菜单由 QToolButton::InstantPopup 自动弹出, 无需手动触发
}

void TestPanel::scheduleMatch() {
    m_debounce->start();
}

void TestPanel::runMatch() {
    m_matchList->clear();
    QList<QTextEdit::ExtraSelection> selections;

    if (m_currentRegex.isEmpty() || !m_currentValid) {
        m_testText->setExtraSelections({});
        m_matchCountLbl->setText(tr("匹配结果:"));
        emit matchCountChanged(0);
        return;
    }

    QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption;
    if (m_flagCaseI->isChecked())     opts |= QRegularExpression::CaseInsensitiveOption;
    if (m_flagMultiLine->isChecked()) opts |= QRegularExpression::MultilineOption;
    if (m_flagUnicodeU->isChecked())  opts |= QRegularExpression::UseUnicodePropertiesOption;
    if (m_flagDotAll->isChecked())    opts |= QRegularExpression::DotMatchesEverythingOption;
    QRegularExpression re(m_currentRegex, opts);
    if (!re.isValid()) {
        m_statusLabel->setText(
            tr("正则错误: %1").arg(re.errorString()));
        m_statusLabel->show();
        m_regexDisplay->setStyleSheet(errorStyleSheet());
        m_testText->setExtraSelections({});
        m_matchCountLbl->setText(tr("匹配结果: 错误"));
        emit matchCountChanged(0);
        return;
    }

    QTextCharFormat fmt;
    fmt.setBackground(Theme::instance().matchHighlight());
    fmt.setFontWeight(QFont::Bold);

    const QString text = m_testText->toPlainText();
    const bool globalMatchEnabled = m_flagGlobalG->isChecked();
    const bool stickyEnabled = m_flagStickyY->isChecked();
    int idx = 0;
    auto appendMatch = [&](const QRegularExpressionMatch &m) -> bool {
        if (!m.hasMatch()) return false;
        if (m.capturedLength() == 0) {
            // 防止零宽匹配死循环 (理论上 globalMatch 已处理, 加保险)
            return false;
        }

        QTextEdit::ExtraSelection sel;
        sel.format = fmt;
        QTextCursor cur(m_testText->document());
        cur.setPosition(m.capturedStart());
        cur.setPosition(m.capturedEnd(), QTextCursor::KeepAnchor);
        sel.cursor = cur;
        selections.append(sel);

        QString itemText = QStringLiteral("[%1] \"%2\"  @%3..%4")
                               .arg(idx)
                               .arg(m.captured())
                               .arg(m.capturedStart())
                               .arg(m.capturedEnd());
        for (int g = 1; g <= m.lastCapturedIndex(); ++g) {
            itemText += QStringLiteral("\n      组%1: \"%2\"")
                            .arg(g).arg(m.captured(g));
        }
        auto *li = new QListWidgetItem(itemText);
        // 把 start/end 存到 item data 里, 点击时拿出来跳转
        li->setData(kRoleStart, m.capturedStart());
        li->setData(kRoleEnd,   m.capturedEnd());
        m_matchList->addItem(li);
        ++idx;
        return true;
    };

    if (stickyEnabled) {
        // sticky: 每次必须从当前位置起始匹配; 一旦在当前位置失败就停止.
        int offset = 0;
        while (offset <= text.size()) {
            const auto m = re.match(text, offset,
                                    QRegularExpression::NormalMatch,
                                    QRegularExpression::AnchorAtOffsetMatchOption);
            if (!m.hasMatch()) break;
            const int next = m.capturedEnd();
            const bool appended = appendMatch(m);
            if (!globalMatchEnabled) break;
            if (!appended || next <= offset) {
                ++offset;
            } else {
                offset = next;
            }
        }
    } else if (globalMatchEnabled) {
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            appendMatch(m);
        }
    } else {
        appendMatch(re.match(text));
    }

    if (idx == 0 && !text.isEmpty()) {
        m_matchList->addItem(new QListWidgetItem(tr("(无匹配)")));
    }
    m_testText->setExtraSelections(selections);

    // 更新匹配数量显示
    if (idx == 0) {
        m_matchCountLbl->setText(tr("匹配结果: 0 个"));
    } else {
        m_matchCountLbl->setText(tr("匹配结果: %1 个  (点击跳转)").arg(idx));
    }
    emit matchCountChanged(idx);
}

void TestPanel::onMatchItemClicked(QListWidgetItem *item) {
    if (!item) return;
    bool ok1 = false, ok2 = false;
    const int start = item->data(kRoleStart).toInt(&ok1);
    const int end   = item->data(kRoleEnd).toInt(&ok2);
    if (!ok1 || !ok2) return;   // "无匹配"占位项

    QTextCursor cur(m_testText->document());
    cur.setPosition(start);
    cur.setPosition(end, QTextCursor::KeepAnchor);
    m_testText->setTextCursor(cur);
    m_testText->ensureCursorVisible();
    m_testText->setFocus();
}

void TestPanel::onReplaceAllClicked() {
    if (m_currentRegex.isEmpty() || !m_currentValid) {
        m_statusLabel->setText(tr("当前正则为空或无效, 无法替换"));
        m_statusLabel->show();
        return;
    }
    QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption;
    if (m_flagCaseI->isChecked())     opts |= QRegularExpression::CaseInsensitiveOption;
    if (m_flagMultiLine->isChecked()) opts |= QRegularExpression::MultilineOption;
    if (m_flagUnicodeU->isChecked())  opts |= QRegularExpression::UseUnicodePropertiesOption;
    if (m_flagDotAll->isChecked())    opts |= QRegularExpression::DotMatchesEverythingOption;
    QRegularExpression re(m_currentRegex, opts);
    if (!re.isValid()) return;

    const QString original = m_testText->toPlainText();
    const QString replacement = m_replaceEdit->text();
    const QString result = QString(original).replace(re, replacement);

    if (result == original) {
        m_statusLabel->setText(tr("没有匹配可以替换"));
        m_statusLabel->show();
        return;
    }

    // 用 QTextCursor 全选替换 → 走文档撤销栈, 用户可 ⌘Z / Ctrl+Z 撤销
    QTextCursor cur(m_testText->document());
    cur.beginEditBlock();
    cur.select(QTextCursor::Document);
    cur.insertText(result);
    cur.endEditBlock();

    m_statusLabel->hide();
    // textChanged 已经触发 dirtied + scheduleMatch
}
