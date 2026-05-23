#include "core/commands.h"

#include "core/block.h"
#include "ui/blockcanvas.h"

namespace {
QString pathToText(const BlockCanvas::BlockPath &path) {
    QStringList parts;
    for (int i : path) parts << QString::number(i + 1);
    return parts.join(QStringLiteral("."));
}
} // namespace

// ===== AddBlockCommand ======================================================

AddBlockCommand::AddBlockCommand(BlockCanvas *canvas, std::unique_ptr<Block> block,
                                 BlockCanvas::BlockPath parentPath, int childIdx,
                                 QUndoCommand *parent)
    : QUndoCommand(parent), m_canvas(canvas),
      m_json(block ? block->toJson() : QJsonObject()),
      m_parentPath(std::move(parentPath)), m_childIdx(childIdx) {
    if (block) {
        setText(QObject::tr("添加 %1").arg(block->displayText()));
    } else {
        setText(QObject::tr("添加积木"));
    }
}

void AddBlockCommand::redo() {
    auto blk = Block::fromJson(m_json);
    if (blk) m_canvas->rawInsert(std::move(blk), m_parentPath, m_childIdx);
}

void AddBlockCommand::undo() {
    BlockCanvas::BlockPath p = m_parentPath; p.append(m_childIdx);
    m_canvas->rawRemoveAt(p);
}

// ===== RemoveBlockCommand ===================================================

RemoveBlockCommand::RemoveBlockCommand(BlockCanvas *canvas,
                                       BlockCanvas::BlockPath path,
                                       QUndoCommand *parent)
    : QUndoCommand(parent), m_canvas(canvas), m_path(std::move(path)) {
    Block *blk = canvas->blockAt(m_path);   // 会同步 group 数据
    if (blk) {
        m_json = blk->toJson();
        setText(QObject::tr("删除 %1").arg(blk->displayText()));
    } else {
        setText(QObject::tr("删除积木"));
    }
}

void RemoveBlockCommand::redo() {
    m_canvas->rawRemoveAt(m_path);
}

void RemoveBlockCommand::undo() {
    auto blk = Block::fromJson(m_json);
    if (!blk) return;
    BlockCanvas::BlockPath parent = m_path; parent.removeLast();
    m_canvas->rawInsert(std::move(blk), parent, m_path.last());
}

// ===== MoveBlockCommand =====================================================

MoveBlockCommand::MoveBlockCommand(BlockCanvas *canvas,
                                   BlockCanvas::BlockPath fromPath,
                                   BlockCanvas::BlockPath toParent, int toChildIdx,
                                   QUndoCommand *parent)
    : QUndoCommand(parent), m_canvas(canvas),
      m_fromPath(std::move(fromPath)),
      m_toParent(std::move(toParent)),
      m_toChildIdx(toChildIdx) {
    setText(QObject::tr("移动积木 %1 → %2.%3")
            .arg(pathToText(m_fromPath))
            .arg(pathToText(m_toParent))
            .arg(toChildIdx + 1));
}

void MoveBlockCommand::redo() {
    // 首次 redo 也要真正搬迁: onItemDragEnded 只负责拖动期间的视觉跟随,
    // 并未做跨容器 reparent, 也没有把顶层拖到新位置后刷新布局.
    // 由命令自己统一做 reparent + relayout, undo/redo 才对称.
    m_canvas->rawMoveAcross(m_fromPath, m_toParent, m_toChildIdx);
    m_firstRun = false;
}

void MoveBlockCommand::undo() {
    // 计算当前所在的 path: toParent + adjustedIdx
    BlockCanvas::BlockPath fromParent = m_fromPath; fromParent.removeLast();
    int adjustedIdx = m_toChildIdx;
    if (fromParent == m_toParent && m_fromPath.last() < m_toChildIdx) {
        adjustedIdx -= 1;
    }
    BlockCanvas::BlockPath cur = m_toParent; cur.append(adjustedIdx);
    m_canvas->rawMoveAcross(cur, fromParent, m_fromPath.last());
}

// ===== EditBlockCommand =====================================================

EditBlockCommand::EditBlockCommand(BlockCanvas *canvas,
                                   BlockCanvas::BlockPath path,
                                   const QJsonObject &oldJson,
                                   const QJsonObject &newJson,
                                   QUndoCommand *parent)
    : QUndoCommand(parent), m_canvas(canvas),
      m_path(std::move(path)),
      m_oldJson(oldJson), m_newJson(newJson) {
    setText(QObject::tr("编辑积木 %1").arg(pathToText(m_path)));
}

void EditBlockCommand::redo() {
    if (m_firstRun) {
        m_firstRun = false;
        return;
    }
    auto blk = Block::fromJson(m_newJson);
    if (blk) m_canvas->rawReplaceAt(m_path, std::move(blk));
}

void EditBlockCommand::undo() {
    auto blk = Block::fromJson(m_oldJson);
    if (blk) m_canvas->rawReplaceAt(m_path, std::move(blk));
}

// ===== ClearAllCommand ======================================================

ClearAllCommand::ClearAllCommand(BlockCanvas *canvas, QUndoCommand *parent)
    : QUndoCommand(parent), m_canvas(canvas) {
    m_snapshot = canvas->toJson();
    setText(QObject::tr("清空 %1 个积木").arg(m_snapshot.size()));
}

void ClearAllCommand::redo() {
    m_canvas->rawClear();
}

void ClearAllCommand::undo() {
    for (int i = 0; i < m_snapshot.size(); ++i) {
        const QJsonObject obj = m_snapshot[i].toObject();
        auto blk = Block::fromJson(obj);
        if (blk) m_canvas->rawInsert(std::move(blk), {}, i);
    }
}
