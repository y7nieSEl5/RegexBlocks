#include "ui/blockcanvas.h"

#include "core/block.h"
#include "core/commands.h"
#include "graphics/blockgraphicsitem.h"
#include "ui/blockpalette.h"
#include "ui/blockeditor.h"
#include "ui/theme.h"
#include "util/fonts.h"

#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QPainter>
#include <QKeyEvent>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUndoStack>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr qreal kCanvasW         = 2400.0;
constexpr qreal kCanvasH         = 600.0;
// 积木行纵向位置: 让块中心 (kBlockY + kHeight/2 ≈ 288) 落在画布中线 (300) 附近,
// 配合 setAlignment(AlignVCenter) 能在大多数视口高度下都视觉居中.
constexpr qreal kBlockY          = 260.0;
constexpr qreal kStartX          = 24.0;
// "拖到这里删除"区: 拖动时显示在画布下方 (相对积木行保留 ~40px 间隔).
constexpr qreal kTrashZoneTop    = 400.0;
constexpr qreal kTrashZoneHeight = 180.0;
// 滑入动画时长
constexpr int   kSlideDurationMs = 100;

bool isQuantifierBlock(const BlockGraphicsItem *item) {
    return item && item->block() && item->block()->type() == BlockType::Quantifier;
}

bool isBindableBase(const BlockGraphicsItem *item) {
    if (!item || !item->block()) return false;
    const BlockType t = item->block()->type();
    return t != BlockType::Quantifier
        && t != BlockType::Anchor
        && t != BlockType::Alternation;
}
} // namespace

// ============================================================================
// 构造 / 析构
// ============================================================================

BlockCanvas::BlockCanvas(QWidget *parent)
    : QGraphicsView(parent),
      m_undoStack(new QUndoStack(this)) {
    m_undoStack->setUndoLimit(100);

    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(0, 0, kCanvasW, kCanvasH);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setBackgroundBrush(Theme::instance().canvasBg());
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setDragMode(QGraphicsView::RubberBandDrag);
    setAcceptDrops(true);
    // 纵向居中: 当视口高度 > 画布高度时, 让画布在视口中垂直居中 (避免积木看起来贴顶).
    setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto *ph = m_scene->addText(
        QStringLiteral("把积木拖到这里 (双击编辑参数, 右键菜单, 拖到下方红色区域删除; 按住 Alt 拖动可只移动主块)"),
        ui::uiFont(14));
    ph->setDefaultTextColor(Theme::instance().placeholderText());
    ph->setPos(kStartX, kBlockY + 16);
    m_placeholder = ph;

    connect(&Theme::instance(), &Theme::themeChanged, this, [this, ph]() {
        setBackgroundBrush(Theme::instance().canvasBg());
        ph->setDefaultTextColor(Theme::instance().placeholderText());
        for (auto *it : m_items) it->update();
        viewport()->update();
    });
}

BlockCanvas::~BlockCanvas() = default;

// ============================================================================
// 路径 helper
// ============================================================================

QList<BlockGraphicsItem*> *BlockCanvas::resolveParent(const BlockPath &parentPath) const {
    if (parentPath.isEmpty()) {
        return const_cast<QList<BlockGraphicsItem*>*>(&m_items);
    }
    auto *cur = const_cast<BlockCanvas*>(this);
    QList<BlockGraphicsItem*> *list = const_cast<QList<BlockGraphicsItem*>*>(&cur->m_items);
    for (int i = 0; i < parentPath.size(); ++i) {
        const int idx = parentPath[i];
        if (idx < 0 || idx >= list->size()) return nullptr;
        BlockGraphicsItem *next = (*list)[idx];
        if (!next || !next->isGroup()) return nullptr;
        list = const_cast<QList<BlockGraphicsItem*>*>(&next->groupChildren());
    }
    return list;
}

BlockGraphicsItem *BlockCanvas::itemAt(const BlockPath &path) const {
    if (path.isEmpty()) return nullptr;
    BlockPath parent = path; parent.removeLast();
    auto *list = resolveParent(parent);
    if (!list) return nullptr;
    const int idx = path.last();
    if (idx < 0 || idx >= list->size()) return nullptr;
    return (*list)[idx];
}

BlockCanvas::BlockPath BlockCanvas::pathOf(BlockGraphicsItem *item) const {
    if (!item) return {};
    // 自底向上沿 parentItem 链构造
    QList<BlockGraphicsItem*> chain;
    BlockGraphicsItem *cur = item;
    while (cur) {
        chain.prepend(cur);
        auto *p = qgraphicsitem_cast<BlockGraphicsItem*>(cur->parentItem());
        cur = p;
    }
    BlockPath path;
    // chain[0] 是顶层, chain[i+1] 是 chain[i] 的视觉子
    int idx = m_items.indexOf(chain[0]);
    if (idx < 0) return {};
    path.append(idx);
    for (int i = 1; i < chain.size(); ++i) {
        const auto &kids = chain[i - 1]->groupChildren();
        int childIdx = kids.indexOf(chain[i]);
        if (childIdx < 0) return {};
        path.append(childIdx);
    }
    return path;
}

Block *BlockCanvas::blockAt(const BlockPath &path) const {
    syncGroupData();
    auto *item = itemAt(path);
    return item ? item->block() : nullptr;
}

void BlockCanvas::syncGroupData() const {
    std::function<void(BlockGraphicsItem*)> walk = [&](BlockGraphicsItem *it) {
        if (!it || !it->isGroup()) return;
        auto *gb = static_cast<GroupBlock*>(it->block());
        gb->children().clear();
        for (auto *child : it->groupChildren()) {
            if (!child) continue;
            walk(child);
            if (child->block()) gb->children().push_back(child->block()->clone());
        }
    };
    for (auto *it : m_items) walk(it);
}

// ============================================================================
// 公共 API (走 undo)
// ============================================================================

void BlockCanvas::appendBlock(std::unique_ptr<Block> block) {
    if (!block) return;
    m_undoStack->push(new AddBlockCommand(this, std::move(block), {}, m_items.size()));
}

void BlockCanvas::insertBlockAt(std::unique_ptr<Block> block, qreal sceneX) {
    if (!block) return;
    int insertIdx = m_items.size();
    for (int i = 0; i < m_items.size(); ++i) {
        const qreal cx = m_items[i]->pos().x() + m_items[i]->boundingRect().width() / 2.0;
        if (sceneX < cx) { insertIdx = i; break; }
    }
    m_undoStack->push(new AddBlockCommand(this, std::move(block), {}, insertIdx));
}

void BlockCanvas::insertBlockAtScenePos(std::unique_ptr<Block> block,
                                        const QPointF &scenePos) {
    if (!block) return;
    int childIdx = 0;
    BlockPath parentPath = dropTargetAt(scenePos, &childIdx);
    m_undoStack->push(new AddBlockCommand(this, std::move(block), parentPath, childIdx));
}

void BlockCanvas::clearBlocks() {
    if (m_items.isEmpty()) return;
    m_undoStack->push(new ClearAllCommand(this));
}

void BlockCanvas::deleteBlockAt(const BlockPath &path) {
    if (path.isEmpty()) return;
    m_undoStack->push(new RemoveBlockCommand(this, path));
}

void BlockCanvas::duplicateBlockAt(const BlockPath &path) {
    syncGroupData();
    auto *item = itemAt(path);
    if (!item || !item->block()) return;
    auto cloned = item->block()->clone();
    BlockPath parent = path; parent.removeLast();
    m_undoStack->push(new AddBlockCommand(this, std::move(cloned), parent, path.last() + 1));
}

void BlockCanvas::replaceBlockAt(const BlockPath &path,
                                 const QJsonObject &oldJson,
                                 const QJsonObject &newJson) {
    if (path.isEmpty()) return;
    if (oldJson == newJson) return;
    m_undoStack->push(new EditBlockCommand(this, path, oldJson, newJson));
}

// ============================================================================
// 原子操作 (供 commands.cpp 调用)
// ============================================================================

void BlockCanvas::rawInsert(std::unique_ptr<Block> block,
                            const BlockPath &parentPath, int childIdx) {
    if (!block) return;
    auto *item = new BlockGraphicsItem(std::move(block));

    // GroupBlock: 把数据中的 m_children drain 为视觉子项 (递归)
    if (item->isGroup()) {
        std::function<void(BlockGraphicsItem*)> drain = [&](BlockGraphicsItem *gi) {
            auto *g = static_cast<GroupBlock*>(gi->block());
            std::vector<std::unique_ptr<Block>> taken;
            taken.swap(g->children());
            for (auto &kb : taken) {
                auto *gc = new BlockGraphicsItem(std::move(kb));
                if (gc->isGroup()) drain(gc);
                gi->insertChildItem(gi->childCount(), gc);
                wireItem(gc);
            }
        };
        drain(item);
    }

    if (parentPath.isEmpty()) {
        m_scene->addItem(item);
        childIdx = std::clamp(childIdx, 0, static_cast<int>(m_items.size()));
        // 预先计算插入位置, 避免插入后停在 (0, 0) 撞 idx=0
        qreal targetX = kStartX;
        for (int i = 0; i < childIdx; ++i) {
            targetX += m_items[i]->boundingRect().width() + BlockGraphicsItem::kSpacing;
        }
        item->setPos(targetX, kBlockY);
        m_items.insert(childIdx, item);
    } else {
        auto *parent = itemAt(parentPath);
        if (!parent || !parent->isGroup()) {
            // fallback: 路径无效就追加到顶层
            m_scene->addItem(item);
            item->setPos(kStartX, kBlockY);
            m_items.append(item);
        } else {
            parent->insertChildItem(childIdx, item);
        }
    }
    wireItem(item);
    relayout();
    updatePlaceholder();
    emit blocksChanged();
}

std::unique_ptr<Block> BlockCanvas::rawRemoveAt(const BlockPath &path) {
    if (path.isEmpty()) return nullptr;
    auto *item = itemAt(path);
    if (!item) return nullptr;

    // GroupBlock: 同步数据, 让 takeBlock 后的 Block 含完整 children
    if (item->isGroup()) {
        std::function<void(BlockGraphicsItem*)> walk = [&](BlockGraphicsItem *it) {
            if (!it || !it->isGroup()) return;
            auto *gb = static_cast<GroupBlock*>(it->block());
            gb->children().clear();
            for (auto *child : it->groupChildren()) {
                if (!child) continue;
                walk(child);
                if (child->block()) gb->children().push_back(child->block()->clone());
            }
        };
        walk(item);
    }

    auto blk = item->takeBlock();

    // 从父序列移除
    if (path.size() == 1) {
        m_items.removeAt(path[0]);
    } else {
        BlockPath parentPath = path; parentPath.removeLast();
        auto *parent = itemAt(parentPath);
        if (parent && parent->isGroup()) {
            parent->takeChildItem(path.last());  // 已 takeChildItem, 仅从列表移除
        }
    }

    // 删除视觉项 (含其所有视觉子, Qt scene 自动处理)
    if (item->scene()) m_scene->removeItem(item);
    item->deleteLater();

    relayout();
    updatePlaceholder();
    emit blocksChanged();
    return blk;
}

void BlockCanvas::rawMoveAcross(const BlockPath &fromPath,
                                const BlockPath &toParent, int toChildIdx) {
    if (fromPath.isEmpty()) return;
    auto *item = itemAt(fromPath);
    if (!item) return;

    // 防御: 不能把 group 移进自己 (路径关系判断)
    if (item->isGroup()) {
        BlockPath check = toParent;
        // toParent 必须不以 fromPath 开头
        if (toParent.size() >= fromPath.size()) {
            bool prefixMatch = true;
            for (int i = 0; i < fromPath.size(); ++i) {
                if (toParent[i] != fromPath[i]) { prefixMatch = false; break; }
            }
            if (prefixMatch) return;  // 自己装自己, 拒绝
        }
    }

    // 从原父序列摘下 (不删除 BlockGraphicsItem, 仅 reparent)
    if (fromPath.size() == 1) {
        m_items.removeAt(fromPath[0]);
        item->setParentItem(nullptr);
        if (!item->scene()) m_scene->addItem(item);
    } else {
        BlockPath parentPath = fromPath; parentPath.removeLast();
        auto *parent = itemAt(parentPath);
        if (parent && parent->isGroup()) {
            parent->takeChildItem(fromPath.last());
        }
    }

    // 调整 toChildIdx: 如果 fromPath 与 toParent 同级且 fromIdx < toChildIdx, 摘除后下标少 1
    BlockPath fromParent = fromPath; fromParent.removeLast();
    int adjustedIdx = toChildIdx;
    if (fromParent == toParent && fromPath.last() < toChildIdx) {
        adjustedIdx = std::max(0, toChildIdx - 1);
    }

    // 插入到目标
    if (toParent.isEmpty()) {
        adjustedIdx = std::clamp(adjustedIdx, 0, static_cast<int>(m_items.size()));
        if (item->parentItem()) item->setParentItem(nullptr);
        if (!item->scene()) m_scene->addItem(item);
        // 计算目标 x
        qreal targetX = kStartX;
        for (int i = 0; i < adjustedIdx; ++i) {
            targetX += m_items[i]->boundingRect().width() + BlockGraphicsItem::kSpacing;
        }
        item->setPos(targetX, kBlockY);
        m_items.insert(adjustedIdx, item);
    } else {
        auto *parent = itemAt(toParent);
        if (!parent || !parent->isGroup()) {
            // fallback: 追加顶层
            if (item->parentItem()) item->setParentItem(nullptr);
            if (!item->scene()) m_scene->addItem(item);
            item->setPos(kStartX, kBlockY);
            m_items.append(item);
        } else {
            parent->insertChildItem(adjustedIdx, item);
        }
    }

    relayout();
    emit blocksChanged();
}

void BlockCanvas::rawReplaceAt(const BlockPath &path, std::unique_ptr<Block> newBlock) {
    if (path.isEmpty() || !newBlock) return;
    auto *item = itemAt(path);
    if (!item) return;
    item->setBlock(std::move(newBlock));
    relayout();
    emit blocksChanged();
}

void BlockCanvas::rawClear() {
    for (auto *it : m_items) {
        if (it->scene()) m_scene->removeItem(it);
        it->deleteLater();
    }
    m_items.clear();
    updatePlaceholder();
    emit blocksChanged();
}

// ============================================================================
// 查询 / 序列化
// ============================================================================

QList<Block*> BlockCanvas::orderedBlocks() const {
    syncGroupData();
    QList<Block*> out;
    out.reserve(m_items.size());
    for (auto *it : m_items) out.append(it->block());
    return out;
}

int BlockCanvas::blockCount() const {
    int n = 0;
    std::function<void(BlockGraphicsItem*)> walk = [&](BlockGraphicsItem *it) {
        if (!it) return;
        n++;
        if (it->isGroup()) {
            for (auto *c : it->groupChildren()) walk(c);
        }
    };
    for (auto *it : m_items) walk(it);
    return n;
}

QJsonArray BlockCanvas::toJson() const {
    syncGroupData();
    QJsonArray arr;
    for (auto *it : m_items) {
        if (it->block()) arr.append(it->block()->toJson());
    }
    return arr;
}

void BlockCanvas::loadJson(const QJsonArray &arr) {
    m_undoStack->clear();
    rawClear();
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject obj = arr[i].toObject();
        auto blk = Block::fromJson(obj);
        if (blk) rawInsert(std::move(blk), {}, m_items.size());
    }
}

// ============================================================================
// 信号连接 / 拖动状态
// ============================================================================

void BlockCanvas::wireItem(BlockGraphicsItem *item) {
    connect(item, &BlockGraphicsItem::changed,       this, &BlockCanvas::relayout);
    connect(item, &BlockGraphicsItem::dragStarted,   this, &BlockCanvas::onItemDragStarted);
    connect(item, &BlockGraphicsItem::dragMovedBy,   this, &BlockCanvas::onItemDragMoved);
    connect(item, &BlockGraphicsItem::dragEnded,     this, &BlockCanvas::onItemDragEnded);
    connect(item, &BlockGraphicsItem::requestEdit,   this, &BlockCanvas::onItemRequestEdit);
    connect(item, &BlockGraphicsItem::requestDuplicate, this, [this](BlockGraphicsItem *bi) {
        BlockPath p = pathOf(bi);
        if (!p.isEmpty()) duplicateBlockAt(p);
    });
    connect(item, &BlockGraphicsItem::requestDelete, this, [this](BlockGraphicsItem *bi) {
        BlockPath p = pathOf(bi);
        if (!p.isEmpty()) deleteBlockAt(p);
    });
    connect(item, &BlockGraphicsItem::requestToggleQuantifierLink,
            this, [this](BlockGraphicsItem *bi, bool linked) {
        BlockPath p = pathOf(bi);
        if (p.isEmpty() || !bi || !bi->block()
            || bi->block()->type() != BlockType::Quantifier) {
            return;
        }
        auto *qb = static_cast<QuantifierBlock*>(bi->block());
        if (qb->linkedToPrevForDrag() == linked) return;
        const QJsonObject oldJson = bi->block()->toJson();
        qb->setLinkedToPrevForDrag(linked);
        bi->refresh();
        relayout();
        const QJsonObject newJson = bi->block()->toJson();
        m_undoStack->push(new EditBlockCommand(this, p, oldJson, newJson));
        emit blocksChanged();
    });
}

void BlockCanvas::onItemDragStarted(BlockGraphicsItem *item) {
    m_dragIncomingQuantifier = false;
    m_dragSourcePath = pathOf(item);
    m_draggedItem    = item;
    m_dragStartScene = item->scenePos();
    m_boundFollowers.clear();
    m_dragDetachQuantifiers = (QApplication::keyboardModifiers() & Qt::AltModifier);

    // 自动绑定紧随其后的量词: 拖动被修饰项时, 量词跟随移动并在释放后重新吸附.
    if (item && !m_dragDetachQuantifiers && !isQuantifierBlock(item)) {
        const BlockPath p = pathOf(item);
        if (!p.isEmpty()) {
            BlockPath parentPath = p;
            parentPath.removeLast();
            auto *siblings = resolveParent(parentPath);
            if (siblings) {
                for (int i = p.last() + 1; i < siblings->size(); ++i) {
                    auto *sib = (*siblings)[i];
                    if (!isQuantifierBlock(sib)) {
                        break;
                    }
                    auto *q = static_cast<QuantifierBlock*>(sib->block());
                    if (!q->linkedToPrevForDrag()) {
                        break;
                    }
                    m_boundFollowers.append(sib);
                }
            }
        }
    }

    // 鬼影宽度 = 主块 + 绑定量词链 (更符合实际将要移动的块组).
    m_ghostWidth = item ? item->boundingRect().width() : 0.0;
    for (auto *f : m_boundFollowers) {
        if (!f) continue;
        m_ghostWidth += BlockGraphicsItem::kSpacing + f->boundingRect().width();
    }

    // 快照当前选中集合 (含主块, 去重). 主块进红区时整组同步高亮;
    // 释放在红区时整组一起删除 (多选拖删 UX).
    m_dragSelection.clear();
    if (item) m_dragSelection.append(item);
    for (auto *f : m_boundFollowers) {
        if (f && f != item && !m_dragSelection.contains(f)) {
            m_dragSelection.append(f);
        }
    }
    // 仅在"被拖主块当前处于选中态"时，才把其他选中项并入组拖动/组删除.
    // 这样可避免点击未选中块开始拖动时, 误把上一次选中项也带上.
    if (item && item->isSelected()) {
        for (auto *sel : m_scene->selectedItems()) {
            auto *bi = qgraphicsitem_cast<BlockGraphicsItem*>(sel);
            if (bi && bi != item && !m_dragSelection.contains(bi)) {
                m_dragSelection.append(bi);
            }
        }
    }

    // 构建 m_dragFollowers (组拖动跟随者): 子树根筛选, 避免父子同选时双重移动.
    // - 排除 primary 本身 (它自己通过 mouseMove 移动)
    // - 排除 primary 的祖先 (祖先移动会带动 primary, 造成 2x delta)
    // - 排除有选中祖先的项 (会被祖先带动)
    m_dragFollowers.clear();
    QSet<BlockGraphicsItem*> selSet;
    for (auto *bi : m_dragSelection) selSet.insert(bi);

    auto isAncestorOfPrimary = [item](BlockGraphicsItem *candidate) {
        if (!item || !candidate) return false;
        QGraphicsItem *p = item->parentItem();
        while (p) {
            if (p == candidate) return true;
            p = p->parentItem();
        }
        return false;
    };
    auto hasSelectedAncestor = [&selSet](BlockGraphicsItem *bi) {
        QGraphicsItem *p = bi ? bi->parentItem() : nullptr;
        while (p) {
            auto *anc = qgraphicsitem_cast<BlockGraphicsItem*>(p);
            if (anc && selSet.contains(anc)) return true;
            p = p->parentItem();
        }
        return false;
    };

    for (auto *bi : m_dragSelection) {
        if (!bi || bi == item) continue;
        if (isAncestorOfPrimary(bi)) continue;
        if (hasSelectedAncestor(bi)) continue;
        m_dragFollowers.insert(bi, bi->pos());
    }

    viewport()->update();
}

void BlockCanvas::onItemDragMoved(BlockGraphicsItem *item, const QPointF &sceneDelta) {
    // 仅处理 primary 的信号. 由于 BlockGraphicsItem 的 pos() 是 parent-local, 而父项在
    // 拖动期间不移动 / 不旋转缩放, 所以 scene 平移量 = 局部平移量, 可直接叠加到 pressPos.
    if (item != m_draggedItem || m_dragFollowers.isEmpty()) return;

    for (auto it = m_dragFollowers.constBegin(); it != m_dragFollowers.constEnd(); ++it) {
        BlockGraphicsItem *bi = it.key();
        if (!bi) continue;
        // blockSignals 阻断 changed -> relayout 级联 (primary.setPos 触发的 relayout 已经把
        // followers 重置到布局位, 这里覆盖回 pressPos + delta, 不需要再次触发 relayout).
        bi->blockSignals(true);
        bi->setPos(it.value() + sceneDelta);
        bi->blockSignals(false);
    }
    viewport()->update();
}

void BlockCanvas::onItemDragEnded(BlockGraphicsItem *item, bool pendingRemove) {
    BlockGraphicsItem *dragged = m_draggedItem;
    QPointF startScene = m_dragStartScene;

    // 取出多选快照供批量删除用, 然后立刻 clear, 避免后续 relayout 再访问陈旧指针.
    QList<BlockGraphicsItem*> selectionSnap = m_dragSelection;
    QList<BlockGraphicsItem*> boundFollowersSnap = m_boundFollowers;
    m_dragSelection.clear();
    m_boundFollowers.clear();
    m_dragDetachQuantifiers = false;
    // 清空组拖动跟随者表. 非红区释放时它们当前位置是 pressPos + delta,
    // 下面触发的 relayout 会在 m_draggedItem = nullptr 后把它们按布局位重排 -> 自然回弹.
    m_dragFollowers.clear();

    m_draggedItem = nullptr;
    m_dragSourcePath.clear();
    // 清除拖动期间显示的蓝色插入线 + 容器高亮 (等价于旧逻辑清 highlightedSlot,
    // 并额外把 m_dropIndicatorX 重置, 否则 drawForeground 仍会绘线).
    clearDropIndicator();
    item->setPendingRemove(false);
    // 其它选中项也清掉 pendingRemove (非删除分支或下面删除前都要清, 保底处理)
    for (auto *bi : selectionSnap) {
        if (bi && bi != item) bi->setPendingRemove(false);
    }

    // 重新计算源路径: 拖动期间 relayout 可能已按 x 对 m_items 重排序,
    // 起始快照 (m_dragSourcePath) 会变成陈旧索引; 这里以当前视觉结构为准.
    BlockPath sourcePath = dragged ? pathOf(dragged) : BlockPath{};

    if (pendingRemove) {
        // 多选拖到红区: 把整个选中快照一起删除, 打成单个 undo macro.
        // 单选情况下 selectionSnap 只含主块, 行为与旧版等价.
        QList<BlockPath> paths;
        for (auto *bi : selectionSnap) {
            if (!bi) continue;
            BlockPath p = pathOf(bi);
            if (!p.isEmpty()) paths.append(p);
        }
        if (!paths.isEmpty()) {
            deleteBlocksAtPaths(std::move(paths));
        }
        viewport()->update();
        return;
    }

    if (sourcePath.isEmpty() || !dragged) {
        relayout();
        viewport()->update();
        return;
    }

    // 计算释放点的 drop 目标.
    // 注意: 此时 m_draggedItem 已被 line 614 置 null, 必须显式把本地 dragged 传给
    // dropTargetAt / snapQuantifierTarget, 它们才能正确排除"被拖块自身".
    const QPointF releaseScene = dragged->scenePos()
        + QPointF(dragged->boundingRect().width() / 2.0, dragged->boundingRect().height() / 2.0);
    int childIdx = 0;
    BlockPath targetParent = dropTargetAt(releaseScene, &childIdx, dragged);
    BlockPath sourceParent = sourcePath; sourceParent.removeLast();
    int sourceChildIdx = sourcePath.last();

    // 量词自动吸附到最近合法目标 (同容器最近可修饰项后方).
    if (isQuantifierBlock(dragged)) {
        int snappedIdx = childIdx;
        auto *attach = snapQuantifierTarget(releaseScene, targetParent, &snappedIdx, dragged);
        if (attach) {
            childIdx = snappedIdx;
        } else {
            // 当前容器无合法目标, 回退到源容器内最近合法目标; 若仍无, 取消本次移动.
            int fallbackIdx = sourceChildIdx;
            auto *fallback = snapQuantifierTarget(releaseScene, sourceParent, &fallbackIdx, dragged);
            if (!fallback) {
                relayout();
                viewport()->update();
                return;
            }
            targetParent = sourceParent;
            childIdx = fallbackIdx;
        }
    }

    // 防御: 不能把容器拖进自己 (递归路径检查)
    if (dragged->isGroup()) {
        BlockPath check = targetParent;
        if (check.size() >= sourcePath.size()) {
            bool prefix = true;
            for (int i = 0; i < sourcePath.size(); ++i) {
                if (check[i] != sourcePath[i]) { prefix = false; break; }
            }
            if (prefix) {
                relayout();
                viewport()->update();
                return;
            }
        }
    }

    // 同位置 → 仅重新布局, 不入 undo
    if (sourceParent == targetParent &&
        (childIdx == sourceChildIdx || childIdx == sourceChildIdx + 1)) {
        relayout();
        viewport()->update();
        return;
    }

    // 入命令栈: 主块移动 + 自动绑定量词重新吸附.
    if (boundFollowersSnap.isEmpty()) {
        m_undoStack->push(new MoveBlockCommand(this, sourcePath, targetParent, childIdx));
    } else {
        m_undoStack->beginMacro(tr("移动积木及量词"));
        m_undoStack->push(new MoveBlockCommand(this, sourcePath, targetParent, childIdx));

        const BlockPath movedPrimary = pathOf(dragged);
        if (!movedPrimary.isEmpty()) {
            BlockPath attachParent = movedPrimary;
            attachParent.removeLast();
            int attachIdx = movedPrimary.last() + 1;
            for (auto *follower : boundFollowersSnap) {
                if (!follower || !follower->block()
                    || follower->block()->type() != BlockType::Quantifier) {
                    continue;
                }
                const BlockPath fp = pathOf(follower);
                if (fp.isEmpty()) continue;
                BlockPath fpParent = fp;
                fpParent.removeLast();
                if (fpParent == attachParent && fp.last() == attachIdx) {
                    ++attachIdx;
                    continue;
                }
                m_undoStack->push(new MoveBlockCommand(this, fp, attachParent, attachIdx));
                ++attachIdx;
            }
        }
        m_undoStack->endMacro();
    }

    const BlockPath newPath = pathOf(dragged);
    auto *newItem = itemAt(newPath);
    if (newItem) {
        animateAndCommit(newItem, releaseScene, []{});
    }
    viewport()->update();
}

void BlockCanvas::onItemRequestEdit(BlockGraphicsItem *item) {
    BlockPath path = pathOf(item);
    if (path.isEmpty()) return;
    const QJsonObject oldJson = item->block()->toJson();
    if (BlockEditor::edit(item->block(), this)) {
        item->refresh();
        relayout();
        const QJsonObject newJson = item->block()->toJson();
        if (newJson != oldJson) {
            m_undoStack->push(new EditBlockCommand(this, path, oldJson, newJson));
        }
        emit blocksChanged();
    }
}

// ============================================================================
// drop 命中测试
// ============================================================================

BlockCanvas::BlockPath BlockCanvas::dropTargetAt(const QPointF &scenePos,
                                                 int *outChildIdx,
                                                 BlockGraphicsItem *excludeItem) const {
    // 优先用显式 excludeItem; 否则回退到 m_draggedItem.
    // onItemDragEnded 阶段 m_draggedItem 已被清空, 必须显式传 dragged 进来.
    BlockGraphicsItem * const skip = excludeItem ? excludeItem : m_draggedItem;

    // 自内向外: 优先命中最深的 GroupBlock 内部
    BlockPath bestPath;
    BlockGraphicsItem *bestGroup = nullptr;

    std::function<void(BlockGraphicsItem*, const BlockPath&)> walk =
        [&](BlockGraphicsItem *item, const BlockPath &path) {
        if (!item || item == skip) return;
        if (!item->isGroup()) return;
        // 先递归子 (内层优先)
        for (int i = 0; i < item->groupChildren().size(); ++i) {
            auto *child = item->groupChildren()[i];
            if (!child) continue;
            if (child == skip) continue;
            if (child->isGroup()) {
                BlockPath childPath = path; childPath.append(i);
                walk(child, childPath);
            }
        }
        // 内层未命中再测自己内部
        if (bestGroup) return;
        const QRectF interior(BlockGraphicsItem::kBracketW, 0,
                              item->boundingRect().width() - 2 * BlockGraphicsItem::kBracketW,
                              item->boundingRect().height());
        const QRectF interiorScene = item->mapToScene(interior).boundingRect();
        if (interiorScene.contains(scenePos)) {
            bestGroup = item;
            bestPath = path;
        }
    };
    for (int i = 0; i < m_items.size(); ++i) {
        BlockPath p; p.append(i);
        walk(m_items[i], p);
    }

    if (bestGroup) {
        const auto &kids = bestGroup->groupChildren();
        int idx = kids.size();
        for (int i = 0; i < kids.size(); ++i) {
            if (kids[i] == skip) continue;
            const qreal cx = kids[i]->scenePos().x() + kids[i]->boundingRect().width() / 2.0;
            if (scenePos.x() < cx) { idx = i; break; }
        }
        if (outChildIdx) *outChildIdx = idx;
        return bestPath;
    }

    // 顶层: 按 x 找位置
    int idx = m_items.size();
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i] == skip) continue;
        const qreal cx = m_items[i]->pos().x() + m_items[i]->boundingRect().width() / 2.0;
        if (scenePos.x() < cx) { idx = i; break; }
    }
    if (outChildIdx) *outChildIdx = idx;
    return {};
}

BlockGraphicsItem *BlockCanvas::snapQuantifierTarget(const QPointF &scenePos,
                                                      const BlockPath &parentPath,
                                                      int *outChildIdx,
                                                      BlockGraphicsItem *excludeItem) const {
    // 优先用显式传入的 excludeItem; 否则回退到 m_draggedItem.
    // onItemDragEnded 阶段 m_draggedItem 已被清空, 必须由调用方传 dragged 进来.
    BlockGraphicsItem * const skip = excludeItem ? excludeItem : m_draggedItem;

    auto *siblings = resolveParent(parentPath);
    if (!siblings || siblings->isEmpty()) return nullptr;

    BlockGraphicsItem *bestBase = nullptr;
    int bestBaseIdx = -1;
    qreal bestDist = std::numeric_limits<qreal>::max();
    for (int i = 0; i < siblings->size(); ++i) {
        BlockGraphicsItem *cand = (*siblings)[i];
        if (!cand || cand == skip) continue;
        if (!isBindableBase(cand)) continue;
        const qreal cx = cand->scenePos().x() + cand->boundingRect().width() / 2.0;
        const qreal dist = std::abs(scenePos.x() - cx);
        if (dist < bestDist) {
            bestDist = dist;
            bestBase = cand;
            bestBaseIdx = i;
        }
    }
    if (!bestBase || bestBaseIdx < 0) return nullptr;

    int insertIdx = bestBaseIdx + 1;
    bool slotOccupied = false;
    while (insertIdx < siblings->size()) {
        BlockGraphicsItem *next = (*siblings)[insertIdx];
        if (!next || next == skip || !isQuantifierBlock(next)) break;
        slotOccupied = true;
        ++insertIdx;
    }
    // base 后面已经有非自身量词 → 拒绝叠加, 与 regexcompiler / regexparser
    // "量词不能修饰量词" 的语义保持一致.
    if (slotOccupied) return nullptr;
    if (outChildIdx) *outChildIdx = insertIdx;
    return bestBase;
}

void BlockCanvas::computeDropIndicator(const QPointF &scenePos) {
    int childIdx = 0;
    BlockPath parentPath = dropTargetAt(scenePos, &childIdx);

    // 高亮目标容器 (drop hover)
    BlockGraphicsItem *targetGroup = parentPath.isEmpty() ? nullptr : itemAt(parentPath);
    if (m_highlightedSlot && m_highlightedSlot != targetGroup) {
        m_highlightedSlot->setSlotHighlighted(false);
        m_highlightedSlot = nullptr;
    }
    if (targetGroup && targetGroup != m_highlightedSlot) {
        targetGroup->setSlotHighlighted(true);
        m_highlightedSlot = targetGroup;
    }

    // 量词吸附目标高亮: 拖动量词时高亮"将被修饰"的块.
    const bool quantifierDrag = m_dragIncomingQuantifier || isQuantifierBlock(m_draggedItem);
    BlockGraphicsItem *attachTarget = nullptr;
    m_quantifierDropInvalid = false;
    if (quantifierDrag) {
        int snappedIdx = childIdx;
        attachTarget = snapQuantifierTarget(scenePos, parentPath, &snappedIdx);
        if (attachTarget) {
            childIdx = snappedIdx; // 插入线跟随"最近合法吸附位"而不是原始鼠标命中位
        }
        m_quantifierDropInvalid = (attachTarget == nullptr);
    }
    if (m_attachHighlight && m_attachHighlight != attachTarget) {
        m_attachHighlight->setAttachHighlighted(false);
        m_attachHighlight = nullptr;
    }
    if (attachTarget && attachTarget != m_attachHighlight) {
        attachTarget->setAttachHighlighted(true);
        m_attachHighlight = attachTarget;
    }

    // 计算 indicator 的 scene x / y / h
    qreal x = kStartX;
    qreal y = kBlockY - 8;
    qreal h = BlockGraphicsItem::kHeight + 16;

    if (parentPath.isEmpty()) {
        // 顶层
        if (childIdx < m_items.size()) {
            x = m_items[childIdx]->pos().x() - BlockGraphicsItem::kSpacing / 2.0;
        } else {
            x = kStartX;
            for (auto *it : m_items) {
                if (it == m_draggedItem) continue;
                x = it->pos().x() + it->boundingRect().width() + BlockGraphicsItem::kSpacing / 2.0;
            }
        }
    } else if (targetGroup) {
        const auto &kids = targetGroup->groupChildren();
        const QRectF gRect = targetGroup->mapToScene(targetGroup->boundingRect()).boundingRect();
        y = gRect.top() + 4;
        h = gRect.height() - 8;
        if (kids.isEmpty()) {
            x = gRect.left() + BlockGraphicsItem::kBracketW + BlockGraphicsItem::kInnerPad;
        } else if (childIdx < kids.size()) {
            x = kids[childIdx]->scenePos().x() - BlockGraphicsItem::kSpacing / 2.0;
        } else {
            auto *last = kids.last();
            x = last->scenePos().x() + last->boundingRect().width() + BlockGraphicsItem::kSpacing / 2.0;
        }
    }

    m_dropIndicatorX = x;
    m_dropIndicatorY = y;
    m_dropIndicatorH = h;
    viewport()->update();
}

void BlockCanvas::clearDropIndicator() {
    m_dropIndicatorX = -1.0;
    m_ghostWidth     = 0.0;
    m_quantifierDropInvalid = false;
    if (m_highlightedSlot) {
        m_highlightedSlot->setSlotHighlighted(false);
        m_highlightedSlot = nullptr;
    }
    if (m_attachHighlight) {
        m_attachHighlight->setAttachHighlighted(false);
        m_attachHighlight = nullptr;
    }
    viewport()->update();
}

// ============================================================================
// 滑入动画
// ============================================================================

void BlockCanvas::animateAndCommit(BlockGraphicsItem *item,
                                   const QPointF &startScenePos,
                                   std::function<void()> /*commitToUndo*/) {
    if (!item) return;
    // 当前 item 的局部 pos = 已布到目标位置
    const QPointF targetLocal = item->pos();
    // 计算"释放时鼠标位置"对应的 item 局部 pos:
    // 把 startScenePos 转到 item 的父项坐标
    QGraphicsItem *parent = item->parentItem();
    QPointF startLocal;
    if (parent) {
        // 让 item 的中心对齐 startScenePos: 转到 parent 局部, 再减去 item 自身 size/2
        const QPointF startInParent = parent->mapFromScene(startScenePos);
        startLocal = startInParent - QPointF(item->boundingRect().width() / 2.0,
                                             item->boundingRect().height() / 2.0);
    } else {
        startLocal = startScenePos - QPointF(item->boundingRect().width() / 2.0,
                                             item->boundingRect().height() / 2.0);
    }
    // 临时把 item 拉回起点, 然后动画到 target
    item->setPos(startLocal);
    auto *anim = new QPropertyAnimation(item, "pos", this);
    anim->setDuration(kSlideDurationMs);
    anim->setStartValue(startLocal);
    anim->setEndValue(targetLocal);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void BlockCanvas::deleteBlocksAtPaths(QList<BlockPath> paths) {
    // 过滤空路径 (顶层根无法删)
    paths.erase(std::remove_if(paths.begin(), paths.end(),
                               [](const BlockPath &p) { return p.isEmpty(); }),
                paths.end());
    if (paths.isEmpty()) return;

    // 去重: 相同路径只保留一次.
    QList<BlockPath> dedup;
    dedup.reserve(paths.size());
    for (const auto &p : paths) {
        if (!dedup.contains(p)) dedup.append(p);
    }

    // 若某路径是另一条路径的后代, 删祖先时会连带删后代, 这里过滤掉后代避免重复命令.
    auto isPrefix = [](const BlockPath &prefix, const BlockPath &path) {
        if (prefix.size() > path.size()) return false;
        for (int i = 0; i < prefix.size(); ++i) {
            if (prefix[i] != path[i]) return false;
        }
        return true;
    };
    QList<BlockPath> filtered;
    filtered.reserve(dedup.size());
    for (const auto &p : dedup) {
        bool coveredByAncestor = false;
        for (const auto &q : dedup) {
            if (q == p) continue;
            if (isPrefix(q, p)) {
                coveredByAncestor = true;
                break;
            }
        }
        if (!coveredByAncestor) filtered.append(p);
    }
    if (filtered.isEmpty()) return;

    // 按路径深度降序 + 同深度按索引降序: 先删子再删父, 先删靠右再删靠左, 避免索引错位.
    std::sort(filtered.begin(), filtered.end(),
              [](const BlockPath &a, const BlockPath &b) {
                  if (a.size() != b.size()) return a.size() > b.size();
                  for (int i = 0; i < a.size(); ++i) {
                      if (a[i] != b[i]) return a[i] > b[i];
                  }
                  return false;
              });

    m_undoStack->beginMacro(QObject::tr("删除 %1 个积木").arg(filtered.size()));
    for (const auto &p : filtered) {
        m_undoStack->push(new RemoveBlockCommand(this, p));
    }
    m_undoStack->endMacro();
}

// ============================================================================
// 事件处理
// ============================================================================

void BlockCanvas::showEvent(QShowEvent *event) {
    QGraphicsView::showEvent(event);
    if (m_initialScrollDone) return;
    m_initialScrollDone = true;
    QTimer::singleShot(0, this, [this]() {
        horizontalScrollBar()->setValue(horizontalScrollBar()->minimum());
        // 初始滚动: 把积木中心 (kBlockY + kHeight/2 ≈ 288) 对齐到视口纵向中点;
        // 视口较高时 std::max 会钳到 0, 配合 AlignVCenter 仍然视觉居中.
        const int targetCenterY = 288;
        verticalScrollBar()->setValue(
            std::max(0, targetCenterY - viewport()->height() / 2));
    });
}

void BlockCanvas::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        QList<BlockPath> paths;
        for (auto *it : m_scene->selectedItems()) {
            auto *bi = qgraphicsitem_cast<BlockGraphicsItem*>(it);
            if (bi) {
                BlockPath p = pathOf(bi);
                if (!p.isEmpty()) paths.append(p);
            }
        }
        if (!paths.isEmpty()) {
            deleteBlocksAtPaths(std::move(paths));
            return;
        }
    }
    QGraphicsView::keyPressEvent(event);
}

void BlockCanvas::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat(BlockPalette::kMimeType)) {
        m_dragIncomingQuantifier = false;
        // 解析鬼影宽度: 用 prototype block 估算
        QJsonParseError err{};
        QJsonDocument doc = QJsonDocument::fromJson(
            event->mimeData()->data(BlockPalette::kMimeType), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            auto blk = Block::fromJson(doc.object());
            if (blk) {
                m_dragIncomingQuantifier = (blk->type() == BlockType::Quantifier);
                // 不入场景, 仅借助一个临时 BlockGraphicsItem 算宽度
                BlockGraphicsItem tmp(std::move(blk));
                m_ghostWidth = tmp.boundingRect().width();
            }
        }
        event->acceptProposedAction();
    } else {
        QGraphicsView::dragEnterEvent(event);
    }
}

void BlockCanvas::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasFormat(BlockPalette::kMimeType)) {
        const QPointF scenePos = mapToScene(event->position().toPoint());
        computeDropIndicator(scenePos);
        event->acceptProposedAction();
    } else {
        QGraphicsView::dragMoveEvent(event);
    }
}

void BlockCanvas::dragLeaveEvent(QDragLeaveEvent *event) {
    m_dragIncomingQuantifier = false;
    clearDropIndicator();
    QGraphicsView::dragLeaveEvent(event);
}

void BlockCanvas::dropEvent(QDropEvent *event) {
    if (!event->mimeData()->hasFormat(BlockPalette::kMimeType)) {
        QGraphicsView::dropEvent(event);
        return;
    }
    const QByteArray bytes = event->mimeData()->data(BlockPalette::kMimeType);
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        m_dragIncomingQuantifier = false;
        clearDropIndicator();
        return;
    }
    auto block = Block::fromJson(doc.object());
    if (!block) {
        m_dragIncomingQuantifier = false;
        clearDropIndicator();
        return;
    }
    const QPointF scenePos = mapToScene(event->position().toPoint());

    int childIdx = 0;
    BlockPath parentPath = dropTargetAt(scenePos, &childIdx);
    if (block->type() == BlockType::Quantifier) {
        int snappedIdx = childIdx;
        auto *attach = snapQuantifierTarget(scenePos, parentPath, &snappedIdx);
        if (!attach) {
            // 没有合法可修饰项, 拒绝本次 drop
            m_quantifierDropInvalid = true;
            viewport()->update();
            clearDropIndicator();
            m_dragIncomingQuantifier = false;
            return;
        }
        childIdx = snappedIdx;
    }

    m_undoStack->push(new AddBlockCommand(this, std::move(block), parentPath, childIdx));

    // 找到刚插入的项, 做滑入动画
    BlockPath newPath = parentPath; newPath.append(childIdx);
    auto *newItem = itemAt(newPath);
    if (newItem) {
        animateAndCommit(newItem, scenePos, []{});
    }

    clearDropIndicator();
    m_dragIncomingQuantifier = false;
    event->acceptProposedAction();
}

// ============================================================================
// 绘制
// ============================================================================

void BlockCanvas::drawBackground(QPainter *painter, const QRectF &rect) {
    QGraphicsView::drawBackground(painter, rect);

    // 拖动期间 (画布积木) 显示"拖到这里删除"区
    if (!m_draggedItem) return;
    const bool entered = m_draggedItem->pendingRemove();

    const QRectF zoneRect(rect.left() + 16, kTrashZoneTop,
                          rect.width() - 32, kTrashZoneHeight);
    QColor fill   = entered ? QColor(244, 67, 54, 70)  : QColor(244, 67, 54, 22);
    QColor border = entered ? QColor("#c62828")        : QColor("#ef9a9a");

    QPen pen(border);
    pen.setStyle(Qt::DashLine);
    pen.setWidthF(entered ? 3.0 : 2.0);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(fill);
    painter->setPen(pen);
    painter->drawRoundedRect(zoneRect, 14, 14);

    painter->setPen(entered ? QColor("#b71c1c") : QColor("#c62828"));
    QFont f = ui::uiFont(18, /*bold=*/true);
    painter->setFont(f);
    const QString text = entered
        ? QObject::tr("松开鼠标 → 删除")
        : QObject::tr("⬇  拖到这里删除  ⬇");
    painter->drawText(zoneRect, Qt::AlignCenter, text);

    if (!m_boundFollowers.isEmpty()) {
        painter->setPen(Theme::instance().textMuted());
        painter->setFont(ui::uiFont(11, false));
        painter->drawText(QRectF(zoneRect.left(), zoneRect.top() - 24,
                                 zoneRect.width(), 20),
                          Qt::AlignCenter,
                          QObject::tr("当前拖动会携带后续量词 (按住 Alt 可只移动主块)"));
    }
    painter->restore();
}

void BlockCanvas::drawForeground(QPainter *painter, const QRectF &rect) {
    QGraphicsView::drawForeground(painter, rect);
    // 持续绘制"被修饰项 -> 量词"连线, 让绑定关系在静态时也可见.
    std::function<void(const QList<BlockGraphicsItem*>&)> drawQuantifierLinks;
    drawQuantifierLinks = [&](const QList<BlockGraphicsItem*> &siblings) {
        for (int i = 1; i < siblings.size(); ++i) {
            auto *right = siblings[i];
            auto *left  = siblings[i - 1];
            if (!right || !left || !right->block() || !left->block()) continue;
            if (right->block()->type() != BlockType::Quantifier) continue;
            auto *q = static_cast<QuantifierBlock*>(right->block());
            if (!q->linkedToPrevForDrag()) continue;
            if (!isBindableBase(left)) continue;

            const QPointF a = left->scenePos()
                + QPointF(left->boundingRect().width(), left->boundingRect().height() / 2.0);
            const QPointF b = right->scenePos()
                + QPointF(0.0, right->boundingRect().height() / 2.0);
            const qreal midX = (a.x() + b.x()) / 2.0;

            const bool emphasize = (left == m_draggedItem || right == m_draggedItem
                                    || m_boundFollowers.contains(right));
            QColor linkColor = emphasize ? QColor(30, 136, 229, 220)
                                         : QColor(30, 136, 229, 95);

            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            QPen pen(linkColor);
            pen.setWidthF(emphasize ? 2.2 : 1.4);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);

            QPainterPath path(a);
            path.cubicTo(QPointF(midX, a.y()),
                         QPointF(midX, b.y()),
                         b);
            painter->drawPath(path);
            painter->restore();
        }
        for (auto *item : siblings) {
            if (item && item->isGroup()) drawQuantifierLinks(item->groupChildren());
        }
    };
    drawQuantifierLinks(m_items);

    if (m_dropIndicatorX >= 0) {
        const QColor indicator = m_quantifierDropInvalid
            ? QColor("#d32f2f")
            : QColor("#1e88e5");

        // 插入线
        QPen linePen(indicator);
        linePen.setWidthF(3.0);
        painter->setPen(linePen);
        painter->drawLine(QPointF(m_dropIndicatorX, m_dropIndicatorY),
                          QPointF(m_dropIndicatorX, m_dropIndicatorY + m_dropIndicatorH));

        // 半透明鬼影矩 (Scratch 风格)
        if (m_ghostWidth > 0) {
            const qreal gh = BlockGraphicsItem::kHeight;
            const qreal gy = m_dropIndicatorY + (m_dropIndicatorH - gh) / 2.0;
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setOpacity(0.35);
            painter->setBrush(indicator);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(QRectF(m_dropIndicatorX - m_ghostWidth / 2.0,
                                            gy, m_ghostWidth, gh),
                                     BlockGraphicsItem::kRadius, BlockGraphicsItem::kRadius);
            painter->restore();
        }

        if (m_quantifierDropInvalid) {
            painter->save();
            painter->setPen(QColor("#d32f2f"));
            painter->setFont(ui::uiFont(11, true));
            painter->drawText(QRectF(m_dropIndicatorX - 120, m_dropIndicatorY - 24, 240, 18),
                              Qt::AlignCenter,
                              QObject::tr("量词需放在可重复项后"));
            painter->restore();
        }
    }

    Q_UNUSED(rect)
}

// ============================================================================
// 布局
// ============================================================================

void BlockCanvas::relayout() {
    // 递归守卫: 下面对 group 调用 layoutChildren() 会 emit changed() 再次触发本函数,
    // 这里直接返回避免栈溢出 (见 m_inRelayout 的定义处说明).
    if (m_inRelayout) return;
    m_inRelayout = true;

    // 拖动期间允许按 x 排序顶层 (用户视觉拖动 -> 重排); 其他场景按列表序
    if (m_draggedItem && m_dragSourcePath.size() == 1) {
        std::sort(m_items.begin(), m_items.end(),
                  [](BlockGraphicsItem *a, BlockGraphicsItem *b) {
                      return a->pos().x() < b->pos().x();
                  });
    }
    qreal x = kStartX;
    for (auto *it : m_items) {
        if (it == m_draggedItem) {
            x += it->boundingRect().width() + BlockGraphicsItem::kSpacing;
            continue;
        }
        it->blockSignals(true);
        it->setPos(x, kBlockY);
        it->blockSignals(false);
        x += it->boundingRect().width() + BlockGraphicsItem::kSpacing;
        // 递归布局 group 子项 (layoutChildren 末尾会 emit changed, 但 m_inRelayout 会拦住递归)
        if (it->isGroup()) it->layoutChildren();
    }

    // 实时判断被拖动积木是否进入垃圾区
    if (m_draggedItem) {
        const QPointF dragScenePos = m_draggedItem->scenePos();
        const qreal centerY = dragScenePos.y() + BlockGraphicsItem::kHeight / 2.0;
        const bool toTrash = (centerY >= kTrashZoneTop);

        // 广播 pendingRemove 到整个多选快照: 主块进红区时,
        // 原地的其他选中项也显示红虚线 + 半透明, 表示"松手一起删".
        for (auto *bi : m_dragSelection) {
            if (bi) bi->setPendingRemove(toTrash);
        }

        // 已有积木拖动也显示 drop 指示线 (蓝色竖线 + GroupBlock 高亮).
        // 先前仅 dragMoveEvent (palette 拖入) 里更新, 导致已有积木拖到
        // GroupBlock 括号里看不到插入位置.
        if (toTrash) {
            clearDropIndicator();
        } else {
            const QPointF dragCenter = dragScenePos + QPointF(
                m_draggedItem->boundingRect().width()  / 2.0,
                m_draggedItem->boundingRect().height() / 2.0);
            computeDropIndicator(dragCenter);
        }
        viewport()->update();
        emit blocksPreviewing();
    }

    m_inRelayout = false;
}

void BlockCanvas::updatePlaceholder() {
    if (m_placeholder) m_placeholder->setVisible(m_items.isEmpty());
}
