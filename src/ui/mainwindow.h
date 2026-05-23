#pragma once

#include <QMainWindow>
#include <QStringList>

class BlockPalette;
class BlockCanvas;
class TestPanel;
class QSplitter;
class QMenu;
class QAction;
class QCloseEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupUi();
    void setupMenu();

    void onNewRecipe();
    void onOpenRecipe();
    void onSaveRecipe();
    bool saveToPath(const QString &path);          // 内部保存; 返回是否成功
    void loadRecipeFile(const QString &path);      // 内部加载 (供 open / recent 共用)
    void loadDefaultExample();
    void loadTemplate(int idx);
    void onRegexImported(const QString &regex);    // 反向解析: 输入框 → 积木
    void showDialectInfo();                        // 帮助菜单: 正则方言说明对话框

    // ---- 未保存修改追踪 ----
    void setDirty(bool dirty);
    void updateTitle();
    bool maybeSave();   // 提示保存; 返回 true 表示用户允许继续 (保存/丢弃), false 取消

    // ---- 最近文件 ----
    void addRecentFile(const QString &path);
    void rebuildRecentMenu();

    // ---- 窗口状态持久化 ----
    void loadSettings();
    void saveSettings();

    // 控件
    QSplitter    *m_splitter   = nullptr;
    BlockPalette *m_palette    = nullptr;
    BlockCanvas  *m_canvas     = nullptr;
    TestPanel    *m_testPanel  = nullptr;

    // 文件 / 状态
    QString      m_currentFilePath;
    bool         m_dirty       = false;

    // 最近文件
    QMenu        *m_recentMenu  = nullptr;
    QStringList  m_recentFiles;
    static constexpr int kMaxRecent = 5;
};
