#pragma once

#include <QGraphicsView>
#include <QList>
#include <QHash>
#include <QPointF>
#include <QJsonArray>
#include <memory>

class QGraphicsScene;
class QUndoStack;
class Block;
class BlockGraphicsItem;

// 中间画布: 持有 Block 树 (含 GroupBlock 嵌套), 渲染并允许拖拽排列.
//
// 公共 API 全部经过 QUndoStack (支持 Cmd+Z / Ctrl+Z 撤销).
// raw* 系列方法是原子操作, 仅供 QUndoCommand 内部调用.
//
// 寻址用 BlockPath = QList<int>:
//   - 空 list  = 顶层根 (作为 parent path; 顶层根本身没有 path)
//   - [3]      = 顶层第 3 个积木
//   - [2, 0]   = 顶层第 2 个 GroupBlock 的第 0 个子积木
class BlockCanvas : public QGraphicsView {
    Q_OBJECT
public:
    using BlockPath = QList<int>;

    explicit BlockCanvas(QWidget *parent = nullptr);
    ~BlockCanvas() override;

    // ===== 公共 API (走 undo stack) =====
    void appendBlock(std::unique_ptr<Block> block);                    // 追加到顶层
    void insertBlockAt(std::unique_ptr<Block> block, qreal sceneX);    // 旧版本 API: 顶层按 x 插入
    void insertBlockAtScenePos(std::unique_ptr<Block> block,
                               const QPointF &scenePos);               // 嵌套支持: 命中到哪层 group 就插入到哪层
    void clearBlocks();
    void deleteBlockAt(const BlockPath &path);
    void duplicateBlockAt(const BlockPath &path);
    // 替换 path 处的积木 (用于编辑器返回新 Block)
    void replaceBlockAt(const BlockPath &path,
                        const QJsonObject &oldJson,
                        const QJsonObject &newJson);

    // ===== 原子操作 (commands.cpp 调用, 不入栈) =====
    void rawInsert(std::unique_ptr<Block> block,
                   const BlockPath &parentPath, int childIdx);
    std::unique_ptr<Block> rawRemoveAt(const BlockPath &path);
    void rawMoveAcross(const BlockPath &fromPath,
                       const BlockPath &toParent, int toChildIdx);
    void rawReplaceAt(const BlockPath &path, std::unique_ptr<Block> newBlock);
    void rawClear();

    // ===== 查询 =====
    // 返回顶层 Block* 列表 (调用前会同步所有 GroupBlock 的 m_children).
    QList<Block*> orderedBlocks() const;
    int           topLevelCount() const { return m_items.size(); }
    int           blockCount()    const;          // 含嵌套总数
    QUndoStack   *undoStack() const { return m_undoStack; }

    // 路径 ↔ 视觉项
    BlockGraphicsItem *itemAt(const BlockPath &path) const;
    BlockPath          pathOf(BlockGraphicsItem *item) const;
    // 取 path 处的 Block (会同步 GroupBlock 数据)
    Block             *blockAt(const BlockPath &path) const;

    // ===== 序列化 =====
    QJsonArray toJson() const;
    void       loadJson(const QJsonArray &arr);   // 不走 undo, 会清空 stack

signals:
    void blocksChanged();      // 积木集合或顺序改变 -> 入 undo, 标脏, 重新匹配
    void blocksPreviewing();   // 拖动期间持续触发 -> 仅刷新右上角文本, 不跑匹配, 不入 undo

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void relayout();
    void onItemDragStarted(BlockGraphicsItem *item);
    void onItemDragMoved(BlockGraphicsItem *item, const QPointF &sceneDelta);
    void onItemDragEnded(BlockGraphicsItem *item, bool pendingRemove);
    void onItemRequestEdit(BlockGraphicsItem *item);

private:
    void wireItem(BlockGraphicsItem *item);
    void updatePlaceholder();

    // 按路径定位父子序列 (内部 helper).
    // 返回的引用指向: 顶层 m_items 容器 (空 path), 或某 group 的 m_groupChildren (非空 path).
    // 注意: 由于 m_items 是 QList<BlockGraphicsItem*>, group 子项也是 QList<BlockGraphicsItem*>,
    //       两者类型一致, 可统一返回视觉项列表.
    QList<BlockGraphicsItem*> *resolveParent(const BlockPath &parentPath) const;

    // 同步: 从视觉树重建所有 GroupBlock 的 m_children 数据 (深拷贝).
    void syncGroupData() const;

    // 根据 scene 坐标找 drop 目标:
    //   返回 parentPath (空 = 顶层), 通过 outChildIdx 返回插入索引.
    // 命中到 GroupBlock 内部时, parentPath 指向该 group, 索引在子序列内计算.
    // excludeItem: 显式排除的块 (默认 nullptr 时回退到 m_draggedItem).
    // 给 onItemDragEnded 用: 它在调用前已经把 m_draggedItem 置 null, 必须显式传 dragged.
    BlockPath dropTargetAt(const QPointF &scenePos, int *outChildIdx,
                           BlockGraphicsItem *excludeItem = nullptr) const;
    // 量词吸附: 依据 scenePos 在 parentPath 对应序列内找"最近可修饰项",
    // 返回被吸附的目标块, 并把 outChildIdx 改成应插入的位置 (该目标后方).
    // 若目标 base 后面已经有非 excludeItem 的量词, 返回 nullptr (拒绝叠加).
    BlockGraphicsItem *snapQuantifierTarget(const QPointF &scenePos,
                                            const BlockPath &parentPath,
                                            int *outChildIdx,
                                            BlockGraphicsItem *excludeItem = nullptr) const;

    // 计算 drop 指示线 (蓝色竖线 + 半透明鬼影) 的 scene 坐标 + 高度
    void computeDropIndicator(const QPointF &scenePos);
    void clearDropIndicator();

    // 滑入动画: 从 startScenePos 100ms 滑到目标 (item 当前位置) , 完成后入 undo.
    void animateAndCommit(BlockGraphicsItem *item,
                          const QPointF &startScenePos,
                          std::function<void()> commitToUndo);

    // 按路径批量删除 (深度降序, 单个 undo macro).
    // keyPressEvent (Delete/Backspace) 与多选拖到红区释放时共用.
    void deleteBlocksAtPaths(QList<BlockPath> paths);

    QGraphicsScene *m_scene = nullptr;
    QList<BlockGraphicsItem*> m_items;
    QGraphicsItem  *m_placeholder = nullptr;
    QUndoStack     *m_undoStack = nullptr;

    // ---- Drop indicator (鼠标拖拽自调色板 / 跨容器期间) ----
    qreal              m_dropIndicatorX = -1.0;
    qreal              m_dropIndicatorY = 0.0;
    qreal              m_dropIndicatorH = 0.0;
    qreal              m_ghostWidth     = 0.0;     // 鬼影宽度 (随被拖动积木而变)
    BlockGraphicsItem *m_highlightedSlot = nullptr; // 当前高亮的容器 (drop hover)
    BlockGraphicsItem *m_attachHighlight = nullptr; // 量词吸附目标高亮
    bool               m_dragIncomingQuantifier = false; // 调色板拖入的是量词
    bool               m_quantifierDropInvalid = false;  // 当前量词放置位置是否无效

    // ---- 画布上已有积木的拖动状态 ----
    BlockPath          m_dragSourcePath;            // 起点路径 (空 = 无拖拽)
    BlockGraphicsItem *m_draggedItem    = nullptr;
    QPointF            m_dragStartScene;            // 拖动起始 scene 位置 (撤销恢复用)
    bool               m_dragDetachQuantifiers = false; // 按住 Alt 开始拖动时, 不携带后续量词
    // 拖动开始时的选中积木快照 (含主块, 已去重).
    // 主块进红色区域时整组同步高亮; 释放时整组一起删除.
    QList<BlockGraphicsItem*> m_dragSelection;
    // 自动绑定跟随的量词块: 当拖动"被修饰项"时, 紧随其后的量词会一起移动,
    // 并在释放时重新吸附到主块后方.
    QList<BlockGraphicsItem*> m_boundFollowers;
    // 组拖动: key = 跟随 primary 一起平移的积木 (子树根, 已排除 primary / primary 的选中祖先 /
    // 有选中祖先的项), value = 拖动开始时该积木的 pos() (parent-local).
    // 每次 primary mouseMove 后, canvas 把 sceneDelta 叠加到每个 follower 的 pressPos.
    QHash<BlockGraphicsItem*, QPointF> m_dragFollowers;

    bool               m_initialScrollDone = false;

    // 递归守卫: layoutChildren() 末尾会 emit changed(), 经 wireItem 连回 relayout().
    // 首次从 relayout 里调用 group->layoutChildren() 会再次触发 relayout, 若不拦截
    // 就会栈溢出崩溃 (用户拖"分组"到画布时首次复现).
    bool               m_inRelayout = false;
};
