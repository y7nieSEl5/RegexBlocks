#pragma once

#include <QDialog>

class Block;
class BlockGraphicsItem;

// 双击积木时弹出的参数编辑对话框 (根据 Block 类型动态构造)
// 用法: 静态方法 edit() -> 阻塞模态, 返回是否修改
class BlockEditor {
public:
    // 弹窗编辑指定 Block (in-place 修改). 返回 true 表示用户确认了变更.
    static bool edit(Block *block, QWidget *parent = nullptr);
};
