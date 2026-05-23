#pragma once

// 撤销/重做命令 (跨平台 Cmd+Z / Ctrl+Z 自动适配, 见 mainwindow.cpp 用 QKeySequence::Undo)
//
// 寻址全部用 BlockCanvas::BlockPath = QList<int>:
//   - 空 list  = 顶层根 (作为 parent path)
//   - [3]      = 顶层第 3 个积木
//   - [2, 0]   = 顶层第 2 个 GroupBlock 的第 0 个子积木

#include <QUndoCommand>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QList>
#include <memory>

#include "ui/blockcanvas.h"   // 为 BlockCanvas::BlockPath

class Block;

// 添加积木: redo 插入到 (parentPath, childIdx), undo 移除该位置
class AddBlockCommand : public QUndoCommand {
public:
    AddBlockCommand(BlockCanvas *canvas, std::unique_ptr<Block> block,
                    BlockCanvas::BlockPath parentPath, int childIdx,
                    QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    BlockCanvas             *m_canvas;
    QJsonObject              m_json;       // 序列化保存, 跨多次 redo/undo 都能重建
    BlockCanvas::BlockPath   m_parentPath;
    int                      m_childIdx;
};

// 删除积木: redo 移除并保存数据 (含嵌套子树), undo 在原位置插回去
class RemoveBlockCommand : public QUndoCommand {
public:
    RemoveBlockCommand(BlockCanvas *canvas, BlockCanvas::BlockPath path,
                       QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    BlockCanvas             *m_canvas;
    QJsonObject              m_json;       // 删除前的快照
    BlockCanvas::BlockPath   m_path;
};

// 跨容器/同容器搬迁积木. 首次 redo 是 no-op (拖动已发生且 BlockCanvas 已视觉 reparent).
// 后续 redo / undo 走 rawMoveAcross.
class MoveBlockCommand : public QUndoCommand {
public:
    MoveBlockCommand(BlockCanvas *canvas,
                     BlockCanvas::BlockPath fromPath,
                     BlockCanvas::BlockPath toParent, int toChildIdx,
                     QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    BlockCanvas             *m_canvas;
    BlockCanvas::BlockPath   m_fromPath;
    BlockCanvas::BlockPath   m_toParent;
    int                      m_toChildIdx;
    bool                     m_firstRun = true;
};

// 编辑积木参数: redo 替换为 newJson, undo 替换为 oldJson; 首次 redo 是 no-op
class EditBlockCommand : public QUndoCommand {
public:
    EditBlockCommand(BlockCanvas *canvas, BlockCanvas::BlockPath path,
                     const QJsonObject &oldJson, const QJsonObject &newJson,
                     QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    BlockCanvas             *m_canvas;
    BlockCanvas::BlockPath   m_path;
    QJsonObject              m_oldJson;
    QJsonObject              m_newJson;
    bool                     m_firstRun = true;
};

// 清空全部: redo 清空, undo 恢复全部 (顶层数组)
class ClearAllCommand : public QUndoCommand {
public:
    ClearAllCommand(BlockCanvas *canvas, QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    BlockCanvas *m_canvas;
    QJsonArray   m_snapshot;
};
