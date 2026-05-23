#pragma once

#include <QGraphicsObject>
#include <QList>
#include <QPointF>
#include <memory>

class Block;

// 视图层: 渲染一个 Block 的圆角彩色积木 + 文字.
// GroupBlock 渲染为 Scratch 风格容器: 两端括号 + 子积木物理嵌入,
// 子积木以 setParentItem 挂在容器下, 通过 m_groupChildren 维护顺序.
class BlockGraphicsItem : public QGraphicsObject {
    Q_OBJECT
public:
    explicit BlockGraphicsItem(std::unique_ptr<Block> block, QGraphicsItem *parent = nullptr);
    ~BlockGraphicsItem() override;

    static constexpr qreal kHeight       = 56.0;
    static constexpr qreal kPaddingX     = 16.0;
    static constexpr qreal kRadius       = 10.0;
    static constexpr qreal kSpacing      = 8.0;   // 积木间距
    static constexpr qreal kBracketW     = 22.0;  // GroupBlock 左右括号区宽度
    static constexpr qreal kInnerPad     = 6.0;   // GroupBlock 内边距 (括号到子项)
    static constexpr qreal kEmptySlotW   = 90.0;  // 空 GroupBlock 占位区宽度
    static constexpr int   kItemType     = QGraphicsItem::UserType + 1;

    int type() const override { return kItemType; }

    Block *block() const { return m_block.get(); }
    void   setBlock(std::unique_ptr<Block> block);
    std::unique_ptr<Block> takeBlock();   // 释放 m_block 给调用方 (用于 reparent 等)
    // 当 Block 内部数据被外部修改后调用, 重新计算尺寸并重绘
    void   refresh();

    bool isDragging()    const { return m_dragging; }
    bool pendingRemove() const { return m_pendingRemove; }
    // 由 BlockCanvas 在拖动期间调用: 根据是否进入"拖到这里删除"区设置
    void setPendingRemove(bool v);

    // ---- GroupBlock 容器 API ------------------------------------------------
    bool isGroup() const;
    // 返回子项指针快照 (顺序与视觉一致). 仅在 isGroup() 时有意义.
    const QList<BlockGraphicsItem*> &groupChildren() const { return m_groupChildren; }
    int  childCount() const { return m_groupChildren.size(); }
    // 在指定位置插入子项: 设置 setParentItem(this), 加入 m_groupChildren.
    // 调用方负责 child 的所有权 (Qt scene 父子关系自动管理生命周期).
    void insertChildItem(int idx, BlockGraphicsItem *child);
    // 移除指定位置的子项, 不释放 (返回原指针, 由调用方 reparent / 删除).
    BlockGraphicsItem *takeChildItem(int idx);
    // 全部清空 (从 scene 删除).
    void clearChildItems();
    // 横向布局所有子项 + 重算宽度. 不递归 (子 group 自己 layoutChildren).
    void layoutChildren();
    // 高亮容器边框 (drop hover 反馈). 仅 isGroup() 有意义.
    void setSlotHighlighted(bool h);
    // 上一次绘制时的"高亮"状态 (BlockCanvas 拖出后清除)
    bool slotHighlighted() const { return m_slotHighlighted; }
    // 通用吸附高亮 (例如量词拖动时提示将绑定到哪个块)
    void setAttachHighlighted(bool h);
    bool attachHighlighted() const { return m_attachHighlighted; }

    QRectF boundingRect() const override;
    void   paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

signals:
    void changed();                                              // 几何变化 -> 触发 relayout
    void requestEdit(BlockGraphicsItem *item);                   // 双击请求编辑
    void requestDuplicate(BlockGraphicsItem *item);              // 右键 -> 复制
    void requestDelete(BlockGraphicsItem *item);                 // 右键 -> 删除
    void requestToggleQuantifierLink(BlockGraphicsItem *item, bool linked); // 右键锁定/解锁量词跟随
    void dragStarted(BlockGraphicsItem *item);                   // 鼠标按下开始拖
    void dragEnded(BlockGraphicsItem *item, bool pendingRemove); // 鼠标释放结束拖, 携带是否在垃圾区
    // 鼠标拖动期间每次 setPos 后发出: sceneDelta = 当前鼠标 scenePos - 按下时 scenePos.
    // 由 BlockCanvas 用来同步移动其它被选中项 (组拖动).
    void dragMovedBy(BlockGraphicsItem *item, const QPointF &sceneDelta);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void     mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void     mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void     mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void     mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void     contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;
    void     hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void     hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

private:
    qreal computeWidth() const;

    std::unique_ptr<Block> m_block;
    bool                   m_hovered = false;
    bool                   m_dragging = false;       // 鼠标按下到松开期间为 true
    bool                   m_pendingRemove = false;  // 由 BlockCanvas 控制: 是否进入垃圾区
    bool                   m_slotHighlighted = false;// GroupBlock: drop hover 高亮
    bool                   m_attachHighlighted = false; // 吸附目标高亮
    QPointF                m_pressItemPos;           // 按下瞬间的 item pos 快照
    QPointF                m_pressScenePos;          // 按下瞬间的 scene 鼠标坐标快照
    mutable qreal          m_cachedWidth = 0;

    // 仅 GroupBlock 使用: 视觉子项, 顺序与 GroupBlock::m_children 镜像.
    // Qt scene 通过 setParentItem 自动管理子项生命周期.
    QList<BlockGraphicsItem*> m_groupChildren;
};
