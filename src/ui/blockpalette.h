#pragma once

#include <QListWidget>
#include <memory>

class Block;

// 左侧积木库: 分组列表 + 拖拽源
// 自定义 MIME: "application/x-regex-block" -> Block 的 JSON 字符串
class BlockPalette : public QListWidget {
    Q_OBJECT
public:
    static const char *kMimeType;

    explicit BlockPalette(QWidget *parent = nullptr);

protected:
    void startDrag(Qt::DropActions supportedActions) override;

private:
    void populate();
    // 添加一个分组标题项 (灰色不可拖拽)
    void addCategoryHeader(const QString &title);
    // 添加一个可拖拽积木项 (基于一个 Block 原型)
    void addBlockItem(std::unique_ptr<Block> prototype);
    // 根据当前 Theme 重新生成 QSS (主题切换时调用)
    void applyThemedStyle();
};
