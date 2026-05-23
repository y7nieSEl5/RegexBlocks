#include "ui/mainwindow.h"

#include "ui/blockpalette.h"
#include "ui/blockcanvas.h"
#include "ui/testpanel.h"
#include "ui/theme.h"
#include "core/block.h"
#include "core/regexcompiler.h"
#include "core/regexparser.h"
#include "core/templates.h"

#include <QApplication>
#include <QSplitter>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QAction>
#include <QActionGroup>
#include <QMessageBox>
#include <QPushButton>
#include <QAbstractButton>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUndoStack>
#include <QSettings>
#include <QCloseEvent>

namespace {
constexpr const char *kFileFilter = "RegexBlocks 配方 (*.regexblocks);;JSON 文件 (*.json);;所有文件 (*)";

// QSettings keys
constexpr const char *kKeyGeometry    = "window/geometry";
constexpr const char *kKeyState       = "window/state";
constexpr const char *kKeySplitter    = "window/splitter";
constexpr const char *kKeyRecentFiles = "files/recent";
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setupUi();
    setupMenu();
    resize(1280, 800);

    // 积木改变 -> 重新编译正则, 更新 TestPanel 显示, 标脏
    // 用户改了积木但还没保存 → 标记当前状态为"脏" (dirty), 关闭窗口时提示保存.
    // 满路径：BlockCanvas::blocksChanged → RegexCompiler::compile → TestPanel::setRegex (更新显示) + MainWindow::setDirty(true)
    connect(m_canvas, &BlockCanvas::blocksChanged, this, [this]() {
        const auto r = RegexCompiler::compile(m_canvas->orderedBlocks());
        m_testPanel->setRegex(r.regex, r.valid, r.warning);
        setDirty(true);
    });

    // 拖动 60Hz 期间持续刷新右上角文本框 (快路径)
    connect(m_canvas, &BlockCanvas::blocksPreviewing, this, [this]() {
        const auto r = RegexCompiler::compile(m_canvas->orderedBlocks());
        m_testPanel->setRegexPreview(r.regex);
    });

    // 用户输入正则字符串 → RegexParser::parse → BlockCanvas::loadJson (替换积木树) + MainWindow::setDirty(true)
    connect(m_testPanel, &TestPanel::regexEditedByUser,
            this, &MainWindow::onRegexImported);

    // 复制代码片段 -> 状态栏提示
    connect(m_testPanel, &TestPanel::snippetCopied, this, [this](const QString &lang) {
        statusBar()->showMessage(tr("已复制 %1 代码片段").arg(lang), 3000);
        // %1：占位符, arg(lang) 会替换 %1 为 lang 的值 (如 "PCRE" / "Python" / "JavaScript" 等), 3000 毫秒后自动清除提示
    });

    // TestPanel 匹配数 -> 状态栏
    connect(m_testPanel, &TestPanel::matchCountChanged, this, [this](int n) {
        statusBar()->showMessage(tr("找到 %1 处匹配  |  积木数 %2") // %2: 占位符, arg(m_canvas->blockCount()) 替换为当前积木总数, 3000ms 后清除提示
                                     .arg(n).arg(m_canvas->blockCount()));
    });

    // TestPanel 用户改了文本 / flags / replacement -> 标脏
    connect(m_testPanel, &TestPanel::dirtied, this, [this]() { setDirty(true); });

    loadDefaultExample();
    // 启动初始示例不应进入撤销历史 / 标脏
    m_canvas->undoStack()->clear();
    setDirty(false);

    // 加载窗口状态 / 最近文件
    loadSettings();
    rebuildRecentMenu();
}

void MainWindow::setupUi() {
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_palette   = new BlockPalette(m_splitter);
    m_canvas    = new BlockCanvas(m_splitter);
    m_testPanel = new TestPanel(m_splitter);

    m_splitter->addWidget(m_palette);
    m_splitter->addWidget(m_canvas);
    m_splitter->addWidget(m_testPanel);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 3);
    m_splitter->setStretchFactor(2, 2);
    m_splitter->setSizes({240, 720, 480});

    setCentralWidget(m_splitter);
    statusBar()->showMessage(tr("就绪"));
}

void MainWindow::setupMenu() {
    // ---- 文件菜单 ----
    auto *fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(tr("新建配方"),    QKeySequence::New,  this, &MainWindow::onNewRecipe);
    fileMenu->addAction(tr("打开配方..."), QKeySequence::Open, this, &MainWindow::onOpenRecipe);
    fileMenu->addAction(tr("保存配方..."), QKeySequence::Save, this, &MainWindow::onSaveRecipe);

    // ---- 最近文件子菜单 ----
    m_recentMenu = fileMenu->addMenu(tr("最近文件"));
    // (内容由 rebuildRecentMenu() 动态填充)

    fileMenu->addSeparator();
    fileMenu->addAction(tr("加载示例: 电话号码"), this, [this]() {
        if (!maybeSave()) return;
        loadDefaultExample();
        m_canvas->undoStack()->clear();
        m_currentFilePath.clear();
        setDirty(false);
    });
    fileMenu->addSeparator();
    auto *quitAct = fileMenu->addAction(tr("退出"), QKeySequence::Quit, this, &QWidget::close);
    // macOS: 自动移到 RegexBlocks 应用菜单 (屏幕左上角应用名菜单)
    quitAct->setMenuRole(QAction::QuitRole);

    // ---- 编辑菜单 (撤销/重做) ----
    auto *editMenu = menuBar()->addMenu(tr("编辑(&E)"));
    auto *undoAct = m_canvas->undoStack()->createUndoAction(this, tr("撤销"));
    undoAct->setShortcut(QKeySequence::Undo);   // macOS: ⌘Z, Win/Linux: Ctrl+Z
    auto *redoAct = m_canvas->undoStack()->createRedoAction(this, tr("重做"));
    redoAct->setShortcut(QKeySequence::Redo);   // macOS: ⌘⇧Z, Windows: Ctrl+Y, Linux: Ctrl+Shift+Z
    editMenu->addAction(undoAct);
    editMenu->addAction(redoAct);

    // ---- 视图菜单 (主题切换) ----
    auto *viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    auto *themeMenu = viewMenu->addMenu(tr("主题"));
    auto *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);

    auto *lightAct = themeMenu->addAction(tr("浅色"));
    lightAct->setCheckable(true);
    lightAct->setActionGroup(themeGroup);
    auto *darkAct = themeMenu->addAction(tr("暗色"));
    darkAct->setCheckable(true);
    darkAct->setActionGroup(themeGroup);

    // 同步当前已应用的主题状态 (main.cpp 已经 applySaved)
    if (Theme::instance().mode() == Theme::Mode::Dark) darkAct->setChecked(true);
    else                                               lightAct->setChecked(true);

    connect(lightAct, &QAction::triggered, this, [] {
        Theme::instance().setMode(Theme::Mode::Light);
    });
    connect(darkAct, &QAction::triggered, this, [] {
        Theme::instance().setMode(Theme::Mode::Dark);
    });

    // ---- 模板菜单 ----
    auto *tplMenu = menuBar()->addMenu(tr("模板(&T)"));
    const auto templates = tpl::allTemplates();
    for (int i = 0; i < templates.size(); ++i) {
        const auto &t = templates[i];
        // QKeySequence(tr("Ctrl+1")) - Mac 自动转 Cmd+1, Windows/Linux 是 Ctrl+1
        auto *act = tplMenu->addAction(
            tr("%1\t%2").arg(t.name, t.sampleRegex),
            QKeySequence(tr("Ctrl+%1").arg(i + 1)),
            this, [this, i]() { loadTemplate(i); });
        act->setStatusTip(t.description);
        act->setToolTip(QString("%1\n生成: %2").arg(t.description, t.sampleRegex));
    }

    // ---- 帮助菜单 ----
    auto *helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(tr("使用说明"), QKeySequence::HelpContents, this, [this]() {
        QMessageBox::information(this, tr("使用说明"),
            tr("1. 从左侧积木库拖拽到中间画布\n"
               "2. 在画布内拖动积木可以重新排序; 拖到下方红色区域可删除\n"
               "   (拖动被修饰项会自动携带后续量词; 按住 Alt 可只移动主块)\n"
               "   (拖动量词会自动吸附到最近合法目标)\n"
               "3. 把积木拖到分组积木 (...) 内部 → 物理嵌入, 形成嵌套结构 (Scratch 风格)\n"
               "4. 双击积木打开参数对话框 (量词/字符集/分组捕获等)\n"
               "5. 选中积木按 Delete (macOS 上是 ⌫) 键删除\n"
               "6. 右键积木可弹出操作菜单 (量词支持锁定/解锁随动)\n"
               "7. 右上角输入框可直接输入正则字符串, Enter 后反向解析回积木\n"
               "8. 右上角'复制 ▾'可生成 PCRE / Python / JS / Java / Rust 代码片段\n"
               "9. 点击右下匹配列表项 → 编辑器自动跳转\n"
               "10. '替换为'中输入替换字符串, 点'替换全部'即可\n"
               "11. 文件菜单可保存/加载配方 (.regexblocks)"));
    });
    helpMenu->addAction(tr("正则方言说明"), this, &MainWindow::showDialectInfo);
    auto *aboutAct = helpMenu->addAction(tr("关于 RegexBlocks"), this, [this]() {
        QMessageBox::about(this, tr("关于 RegexBlocks"),
            tr("<h3>RegexBlocks</h3>"
               "<p>拖拽式正则表达式测试器</p>"
               "<p>Qt 程序设计实习课程作业</p>"
               "<p>使用 Qt %1 构建</p>"
               "<p>跨平台支持: macOS / Windows / Linux</p>").arg(qVersion()));
    });
    // macOS: 自动移到应用菜单顶部 (与系统其他 app 一致)
    aboutAct->setMenuRole(QAction::AboutRole);

    auto *aboutQtAct = helpMenu->addAction(tr("关于 Qt"), qApp, &QApplication::aboutQt);
    aboutQtAct->setMenuRole(QAction::AboutQtRole);
}

void MainWindow::onNewRecipe() {
    if (!maybeSave()) return;
    m_canvas->clearBlocks();           // 走 undo, 用户可以撤销
    m_canvas->undoStack()->clear();
    m_currentFilePath.clear();
    setDirty(false);
    statusBar()->showMessage(tr("已新建配方"), 3000);
}

void MainWindow::onOpenRecipe() {
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开配方"), {}, tr(kFileFilter));
    if (path.isEmpty()) return;
    loadRecipeFile(path);
}

void MainWindow::loadRecipeFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("打开失败"), f.errorString());
        return;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, tr("文件格式错误"),
                             tr("无法解析 JSON: %1").arg(err.errorString()));
        return;
    }
    const QJsonObject obj = doc.object();
    const int version = obj.value("version").toInt(1);
    m_canvas->loadJson(obj.value("blocks").toArray());
    if (obj.contains("testText")) {
        m_testPanel->setTestText(obj.value("testText").toString());
    }
    // v2 字段: flags
    QStringList flagsList;
    for (const auto &v : obj.value("flags").toArray()) {
        flagsList << v.toString();
    }
    // v4 前没有 g 选项, 历史行为等价于全局匹配, 这里做兼容迁移.
    if (version < 4 && !flagsList.contains("g")) {
        flagsList << "g";
    }
    m_testPanel->setFlags(flagsList);
    // v3 字段: replacement (可选, 兼容)
    if (obj.contains("replacement")) {
        m_testPanel->setReplacement(obj.value("replacement").toString());
    } else {
        m_testPanel->setReplacement({});
    }

    m_currentFilePath = path;
    setDirty(false);
    addRecentFile(path);
    statusBar()->showMessage(tr("已加载: %1").arg(path), 3000);
}

void MainWindow::onSaveRecipe() {
    QString path = m_currentFilePath;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, tr("保存配方"),
            QStringLiteral("untitled.regexblocks"), tr(kFileFilter));
        if (path.isEmpty()) return;
    }
    saveToPath(path);
}

bool MainWindow::saveToPath(const QString &path) {
    QJsonObject obj;
    obj["version"]  = 4;
    obj["blocks"]   = m_canvas->toJson();
    obj["testText"] = m_testPanel->testText();
    QJsonArray flagsArr;
    for (const auto &f : m_testPanel->flags()) flagsArr.append(f);
    obj["flags"] = flagsArr;
    obj["replacement"] = m_testPanel->replacement();

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("保存失败"), f.errorString());
        return false;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    m_currentFilePath = path;
    setDirty(false);
    addRecentFile(path);
    statusBar()->showMessage(tr("已保存: %1").arg(path), 3000);
    return true;
}

void MainWindow::loadDefaultExample() {
    m_canvas->clearBlocks();
    m_canvas->appendBlock(std::make_unique<CharClassBlock>("\\d"));
    m_canvas->appendBlock(std::make_unique<QuantifierBlock>(3, 3));
    m_canvas->appendBlock(std::make_unique<LiteralBlock>("-"));
    m_canvas->appendBlock(std::make_unique<CharClassBlock>("\\d"));
    m_canvas->appendBlock(std::make_unique<QuantifierBlock>(4, 4));
}

void MainWindow::loadTemplate(int idx) {
    if (!maybeSave()) return;
    const auto templates = tpl::allTemplates();
    if (idx < 0 || idx >= templates.size()) return;
    const auto &t = templates[idx];

    // 用 macro 把"清空 + N 个添加"合并成单步撤销
    auto *stack = m_canvas->undoStack();
    stack->beginMacro(tr("加载模板: %1").arg(t.name));
    m_canvas->clearBlocks();
    auto blocks = t.build();
    for (auto &b : blocks) {
        m_canvas->appendBlock(std::move(b));
    }
    stack->endMacro();
    m_currentFilePath.clear();
    setDirty(false);   // 模板视为新起点
    stack->clear();    // 模板加载也不保留历史 (展示用)
    statusBar()->showMessage(tr("已加载模板: %1  (%2)")
                                 .arg(t.name, t.sampleRegex), 4000);
}

// ============================================================================
// 未保存修改追踪
// ============================================================================

void MainWindow::setDirty(bool dirty) {
    if (m_dirty == dirty) return;
    m_dirty = dirty;
    updateTitle();
}

void MainWindow::updateTitle() {
    const QString fileName = m_currentFilePath.isEmpty()
                                 ? tr("未命名")
                                 : QFileInfo(m_currentFilePath).fileName();
    const QString star = m_dirty ? QStringLiteral(" *") : QString();
    // [*] 占位符跨平台; macOS 通常不显示 [*], 但我们手动加 ' *' 保证一致体验
    setWindowTitle(tr("RegexBlocks - %1%2").arg(fileName, star));
    setWindowModified(m_dirty);   // macOS 关闭按钮中圆点也会被点亮
}

bool MainWindow::maybeSave() {
    if (!m_dirty) return true;
    const auto btn = QMessageBox::question(
        this, tr("有未保存的修改"),
        tr("当前配方有未保存的修改, 是否保存?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (btn == QMessageBox::Cancel)  return false;
    if (btn == QMessageBox::Discard) return true;
    // Save: 走 onSaveRecipe → 失败时 m_dirty 仍为 true
    onSaveRecipe();
    return !m_dirty;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    saveSettings();
    event->accept();
}

// ============================================================================
// 最近文件
// ============================================================================

void MainWindow::addRecentFile(const QString &path) {
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > kMaxRecent) m_recentFiles.removeLast();
    rebuildRecentMenu();
    // 立即落盘, 即使未关闭窗口也持久化
    QSettings().setValue(kKeyRecentFiles, m_recentFiles);
}

void MainWindow::rebuildRecentMenu() {
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    if (m_recentFiles.isEmpty()) {
        auto *empty = m_recentMenu->addAction(tr("(无最近文件)"));
        empty->setEnabled(false);
        return;
    }
    int n = 1;
    for (const QString &p : m_recentFiles) {
        const QString shown = QStringLiteral("&%1  %2")
                                  .arg(n++)
                                  .arg(QFileInfo(p).fileName());
        auto *act = m_recentMenu->addAction(shown);
        act->setToolTip(p);
        act->setStatusTip(p);
        connect(act, &QAction::triggered, this, [this, p]() {
            if (!QFile::exists(p)) {
                QMessageBox::warning(this, tr("文件不存在"),
                    tr("文件已被移动或删除:\n%1").arg(p));
                m_recentFiles.removeAll(p);
                rebuildRecentMenu();
                QSettings().setValue(kKeyRecentFiles, m_recentFiles);
                return;
            }
            if (!maybeSave()) return;
            loadRecipeFile(p);
        });
    }
    m_recentMenu->addSeparator();
    auto *clearAct = m_recentMenu->addAction(tr("清空最近文件"));
    connect(clearAct, &QAction::triggered, this, [this]() {
        m_recentFiles.clear();
        rebuildRecentMenu();
        QSettings().setValue(kKeyRecentFiles, m_recentFiles);
    });
}

// ============================================================================
// 窗口状态持久化
// ============================================================================

void MainWindow::loadSettings() {
    QSettings s;
    const QByteArray geom = s.value(kKeyGeometry).toByteArray();
    if (!geom.isEmpty()) restoreGeometry(geom);
    const QByteArray state = s.value(kKeyState).toByteArray();
    if (!state.isEmpty()) restoreState(state);
    const QByteArray sp = s.value(kKeySplitter).toByteArray();
    if (!sp.isEmpty() && m_splitter) m_splitter->restoreState(sp);
    m_recentFiles = s.value(kKeyRecentFiles).toStringList();
    // 截断到上限
    while (m_recentFiles.size() > kMaxRecent) m_recentFiles.removeLast();
}

void MainWindow::saveSettings() {
    QSettings s;
    s.setValue(kKeyGeometry,    saveGeometry());
    s.setValue(kKeyState,       saveState());
    if (m_splitter) s.setValue(kKeySplitter, m_splitter->saveState());
    s.setValue(kKeyRecentFiles, m_recentFiles);
}

// ============================================================================
// 反向解析: 用户在右上角输入框确认 -> 解析为积木树并替换画布
// ============================================================================

void MainWindow::onRegexImported(const QString &regex) {
    if (regex.isEmpty()) return;
    auto result = RegexParser::parse(regex);
    if (!result.ok) {
        // 不支持的特性 / 解析错误 → 弹对话框: 取消 / 作字面量整段导入
        const QString hint = result.errorPos >= 0
            ? tr("位置 %1: %2").arg(result.errorPos).arg(result.error)
            : result.error;
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("无法解析正则"));
        box.setText(tr("反向解析失败:\n%1\n\n是否把整段字符串作为字面量导入?")
                        .arg(hint));
        QPushButton *cancel  = box.addButton(tr("取消"), QMessageBox::RejectRole);
        QPushButton *literal = box.addButton(tr("作字面量导入"), QMessageBox::AcceptRole);
        box.setDefaultButton(cancel);
        box.exec();
        if (box.clickedButton() == static_cast<QAbstractButton*>(literal)) {
            auto *stack = m_canvas->undoStack();
            stack->beginMacro(tr("作字面量导入: %1").arg(regex));
            m_canvas->clearBlocks();
            m_canvas->appendBlock(std::make_unique<LiteralBlock>(regex));
            stack->endMacro();
        }
        return;
    }
    // 成功: 用 macro 把 "清空 + N 个添加" 合并成单步撤销
    auto *stack = m_canvas->undoStack();
    stack->beginMacro(tr("从正则导入: %1").arg(regex));
    m_canvas->clearBlocks();
    for (auto &b : result.blocks) {
        m_canvas->appendBlock(std::move(b));
    }
    stack->endMacro();
    statusBar()->showMessage(tr("已导入正则 (%1 个顶层积木)")
                                 .arg(result.blocks.size()), 3000);
}

// ============================================================================
// 帮助 -> 正则方言说明
// ============================================================================

void MainWindow::showDialectInfo() {
    QMessageBox box(this);
    box.setWindowTitle(tr("正则方言说明"));
    box.setIcon(QMessageBox::Information);
    box.setTextFormat(Qt::RichText);
    box.setText(tr(
        "<h3>RegexBlocks 基于 PCRE2 兼容的正则方言</h3>"
        "<p>底层引擎: Qt <code>QRegularExpression</code> (Perl-Compatible Regular Expressions 2).</p>"
        "<h4>跨语言可移植性</h4>"
        "<p>本工具当前 22 个内置积木 + GroupBlock 涉及的特性 (字符类、量词、字符集、锚点、分支、分组), "
        "在以下方言上语义一致, 生成的正则可以原样使用:</p>"
        "<ul>"
        "<li><b>Python</b> <code>re</code> 模块</li>"
        "<li><b>JavaScript</b> <code>RegExp</code> (/regex/flags)</li>"
        "<li><b>Java</b> <code>java.util.regex.Pattern</code></li>"
        "<li><b>Rust</b> <code>regex</code> crate (基于 RE2)</li>"
        "</ul>"
        "<h4>刻意未支持的方言敏感特性</h4>"
        "<ul>"
        "<li>反向引用 <code>\\1</code> — Rust regex (RE2) 不支持</li>"
        "<li>环视 <code>(?=...) (?!...) (?&lt;=...) (?&lt;!...)</code> — Rust regex (RE2) 不支持</li>"
        "<li>命名分组语法差异: PCRE/Java <code>(?&lt;name&gt;)</code> vs Python <code>(?P&lt;name&gt;)</code> — 解析时统一接受, 输出统一为普通分组</li>"
        "<li>占有量词 <code>++ *+ {..}+</code> — Java 独有, 其他不支持</li>"
        "<li>POSIX 字符类 <code>[[:alpha:]]</code> — JS/Rust 不支持</li>"
        "<li>边界变体 <code>\\A \\z \\Z</code> — JS 不支持</li>"
        "</ul>"
        "<p><b>结论:</b> 当前积木集生成的正则在 5 门语言中通用. "
        "右上角'复制 ▾'菜单可一键导出对应语言的代码片段.</p>"));
    box.exec();
}
