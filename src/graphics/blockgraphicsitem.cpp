#include "graphics/blockgraphicsitem.h"

#include "core/block.h"
#include "util/fonts.h"

#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsScene>
#include <QMenu>
#include <QKeySequence>
#include <algorithm>

namespace {
QFont blockFont() {
    return ui::uiFont(13, /*bold=*/true);
}
QFont bracketFont() {
    return ui::uiFont(22, /*bold=*/true);
}
QFont smallLabelFont() {
    return ui::uiFont(8, /*bold=*/true);
}
} // namespace

BlockGraphicsItem::BlockGraphicsItem(std::unique_ptr<Block> block, QGraphicsItem *parent)
    : QGraphicsObject(parent),
      m_block(std::move(block)) {
    // 注意: 故意不设置 ItemIsMovable - 自己在 mouseMoveEvent 里用 scene 增量平移,
    // 避免 Qt 默认 ItemIsMovable 在某些情况下把 item-local press pos 当 scene pos 用,
    // 导致首次 mouseMove 把积木瞬间锚到 (0,0).
    setFlags(QGraphicsItem::ItemIsSelectable
             | QGraphicsItem::ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    m_cachedWidth = computeWidth();
}

BlockGraphicsItem::~BlockGraphicsItem() = default;

void BlockGraphicsItem::setBlock(std::unique_ptr<Block> block) {
    prepareGeometryChange();
    m_block = std::move(block);
    m_cachedWidth = computeWidth();
    update();
    emit changed();
}

std::unique_ptr<Block> BlockGraphicsItem::takeBlock() {
    return std::move(m_block);
}

void BlockGraphicsItem::refresh() {
    prepareGeometryChange();
    m_cachedWidth = computeWidth();
    update();
    emit changed();
}

bool BlockGraphicsItem::isGroup() const {
    return m_block && m_block->type() == BlockType::Group;
}

void BlockGraphicsItem::insertChildItem(int idx, BlockGraphicsItem *child) {
    if (!child || !isGroup()) return;
    idx = std::clamp(idx, 0, static_cast<int>(m_groupChildren.size()));
    child->setParentItem(this);
    m_groupChildren.insert(idx, child);
    layoutChildren();
}

BlockGraphicsItem *BlockGraphicsItem::takeChildItem(int idx) {
    if (idx < 0 || idx >= m_groupChildren.size()) return nullptr;
    auto *child = m_groupChildren.takeAt(idx);
    if (child) child->setParentItem(nullptr);
    layoutChildren();
    return child;
}

void BlockGraphicsItem::clearChildItems() {
    for (auto *c : m_groupChildren) {
        if (c) {
            if (c->scene()) c->scene()->removeItem(c);
            c->deleteLater();
        }
    }
    m_groupChildren.clear();
    layoutChildren();
}

void BlockGraphicsItem::layoutChildren() {
    if (!isGroup()) return;
    prepareGeometryChange();

    // 若有子项正在被拖动, 按 x 重排 (Scratch 风格: 其它子项滑动让位),
    // 并在布局循环里跳过该子项 -- 让它跟随鼠标而不是被拍回槽位.
    // 判断条件: 直接子项中存在 isDragging() 的项.
    bool hasDraggingChild = false;
    for (auto *c : m_groupChildren) {
        if (c && c->isDragging()) { hasDraggingChild = true; break; }
    }
    if (hasDraggingChild) {
        std::sort(m_groupChildren.begin(), m_groupChildren.end(),
                  [](BlockGraphicsItem *a, BlockGraphicsItem *b) {
                      return a->pos().x() < b->pos().x();
                  });
    }

    qreal x = kBracketW + kInnerPad;
    for (auto *child : m_groupChildren) {
        if (!child) continue;
        if (child->isDragging()) {
            // 预留被拖动子项的宽度 (让其它子项避开), 本身位置由 mouseMove 控制
            x += child->boundingRect().width() + kSpacing;
            continue;
        }
        // 子项也可能是 group, 先递归布局自己再放置
        child->layoutChildren();
        const qreal y = (kHeight - child->boundingRect().height()) / 2.0;
        child->setPos(x, y);
        x += child->boundingRect().width() + kSpacing;
    }
    m_cachedWidth = computeWidth();
    update();
    emit changed();
}

void BlockGraphicsItem::setSlotHighlighted(bool h) {
    if (m_slotHighlighted == h) return;
    m_slotHighlighted = h;
    update();
}

void BlockGraphicsItem::setAttachHighlighted(bool h) {
    if (m_attachHighlighted == h) return;
    m_attachHighlighted = h;
    update();
}

qreal BlockGraphicsItem::computeWidth() const {
    if (!m_block) return 80.0;
    if (isGroup()) {
        qreal childrenW = 0;
        if (m_groupChildren.isEmpty()) {
            childrenW = kEmptySlotW;
        } else {
            for (int i = 0; i < m_groupChildren.size(); ++i) {
                auto *c = m_groupChildren[i];
                if (!c) continue;
                childrenW += c->boundingRect().width();
                if (i < m_groupChildren.size() - 1) childrenW += kSpacing;
            }
        }
        return kBracketW * 2 + kInnerPad * 2 + childrenW;
    }
    QFontMetrics fm(blockFont());
    const qreal textW = fm.horizontalAdvance(m_block->displayText());
    return std::max<qreal>(80.0, textW + kPaddingX * 2);
}

QRectF BlockGraphicsItem::boundingRect() const {
    return QRectF(0, 0, m_cachedWidth, kHeight);
}

void BlockGraphicsItem::paint(QPainter *painter,
                              const QStyleOptionGraphicsItem *option,
                              QWidget * /*widget*/) {
    if (!m_block) return;

    const QRectF rect = boundingRect();
    const QColor base = m_block->color();
    QColor border = isSelected() ? QColor("#1565C0") :
                          (m_hovered ? base.darker(140) : base.darker(120));
    if (m_slotHighlighted || m_attachHighlighted) {
        border = QColor("#1e88e5");
    }

    QLinearGradient grad(rect.topLeft(), rect.bottomLeft());
    grad.setColorAt(0.0, base.lighter(115));
    grad.setColorAt(1.0, base);

    painter->setRenderHint(QPainter::Antialiasing, true);

    // 拖出阈值反馈: 半透明 + 红色虚线边框
    if (m_pendingRemove) {
        painter->setOpacity(0.5);
        QPen redPen(QColor("#d32f2f"));
        redPen.setStyle(Qt::DashLine);
        redPen.setWidthF(2.5);
        painter->setBrush(grad);
        painter->setPen(redPen);
        painter->drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
    } else {
        painter->setBrush(grad);
        QPen pen(border);
        pen.setWidthF((isSelected() || m_slotHighlighted || m_attachHighlighted) ? 2.8 : 1.5);
        painter->setPen(pen);
        painter->drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
    }

    if (isGroup()) {
        // GroupBlock 专属绘制: 两端括号 + 顶部小标签 + 空槽提示
        auto *gb = static_cast<GroupBlock*>(m_block.get());

        // 左/右括号字符
        painter->setFont(bracketFont());
        painter->setPen(Qt::white);
        const QRectF leftBracket(0, 0, kBracketW, kHeight);
        const QRectF rightBracket(rect.width() - kBracketW, 0, kBracketW, kHeight);
        painter->drawText(leftBracket,  Qt::AlignCenter, QStringLiteral("("));
        painter->drawText(rightBracket, Qt::AlignCenter, QStringLiteral(")"));

        // 左上小标签
        painter->setFont(smallLabelFont());
        painter->setPen(QColor(255, 255, 255, 200));
        painter->drawText(rect.adjusted(6, 4, -6, -4), Qt::AlignTop | Qt::AlignLeft,
                          gb->capturing() ? QStringLiteral("分组 (...)")
                                          : QStringLiteral("非捕获 (?:...)"));

        // 空槽: 画虚线占位 + 灰文字
        if (m_groupChildren.isEmpty()) {
            QPen ph(QColor(255, 255, 255, 160));
            ph.setStyle(Qt::DashLine);
            ph.setWidthF(1.5);
            painter->setPen(ph);
            painter->setBrush(Qt::NoBrush);
            const QRectF slot(kBracketW + kInnerPad, 8,
                              kEmptySlotW, kHeight - 16);
            painter->drawRoundedRect(slot, 6, 6);
            painter->setFont(ui::uiFont(11));
            painter->setPen(QColor(255, 255, 255, 200));
            painter->drawText(slot, Qt::AlignCenter, QObject::tr("拖积木到这里"));
        }
    } else {
        // 普通积木: 中间显示文字 + 左上类别标签
        painter->setPen(Qt::white);
        painter->setFont(blockFont());
        painter->drawText(rect, Qt::AlignCenter, m_block->displayText());

        painter->setFont(smallLabelFont());
        painter->setPen(QColor(255, 255, 255, 160));
        painter->drawText(rect.adjusted(8, 4, -8, -4), Qt::AlignTop | Qt::AlignLeft,
                          m_block->category());

        // 量词若处于"不跟随拖动"模式, 右上角显示 free 标签
        if (m_block->type() == BlockType::Quantifier) {
            auto *q = static_cast<QuantifierBlock*>(m_block.get());
            if (!q->linkedToPrevForDrag()) {
                painter->setFont(smallLabelFont());
                painter->setPen(QColor(255, 255, 255, 210));
                painter->drawText(rect.adjusted(6, 4, -6, -4),
                                  Qt::AlignTop | Qt::AlignRight,
                                  QStringLiteral("free"));
            }
        }
    }

    Q_UNUSED(option)
}

QVariant BlockGraphicsItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged) {
        // 位置变化只通知 canvas; 是否进入垃圾区由 BlockCanvas::relayout 判断后回调 setPendingRemove
        emit changed();
    }
    return QGraphicsObject::itemChange(change, value);
}

void BlockGraphicsItem::setPendingRemove(bool v) {
    if (m_pendingRemove == v) return;
    m_pendingRemove = v;
    update();
}

void BlockGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        // 记录按下瞬间的 item / 鼠标 scene 快照, 后续 mouseMove 用绝对增量计算新 pos
        m_dragging      = true;
        m_pendingRemove = false;
        m_pressItemPos  = pos();
        m_pressScenePos = event->scenePos();

        // 单选刷新: 无 Cmd/Ctrl 修饰键时, 立即清空其它选中项.
        // Qt 默认只在"未选中项被点击"时清, 已选中项被点击时保留多选状态;
        // 这导致 Cmd+click 多选 / 橡皮筋框选累积后, 后续单击拖动会把别人一起带走.
        // 这里把"清空"提前到按下时, 保证只拖动当前块.
        // macOS 上 Qt::ControlModifier 已自动映射到 Cmd 键, 无需平台特判.
        const bool multiSelect = event->modifiers() & Qt::ControlModifier;
        if (!multiSelect && scene()) {
            const auto sel = scene()->selectedItems();
            for (auto *si : sel) {
                if (si != this) si->setSelected(false);
            }
        }

        // 先走基类 mousePress, 让 Qt 先完成选中态更新 (单选/多选语义),
        // 再通知 canvas 读取 selectedItems, 避免读到上一次选中快照.
        QGraphicsObject::mousePressEvent(event);
        emit dragStarted(this);
        return;
    }
    QGraphicsObject::mousePressEvent(event);
}

void BlockGraphicsItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        // 绝对增量法: 新 pos = 按下时 item pos + (当前鼠标 scene pos − 按下时鼠标 scene pos).
        // 对顶层项, pos 在 scene 坐标; 对 group 子项, pos 在 group 局部坐标.
        // 由于 group 在拖动期间不动, scene 增量等于局部增量, 同样有效.
        const QPointF delta  = event->scenePos() - m_pressScenePos;
        const QPointF newPos = m_pressItemPos + delta;
        setPos(newPos);
        // setPos 会触发 changed() -> BlockCanvas::relayout() (同步).
        // 之后再发 dragMovedBy, BlockCanvas 在 slot 里把 delta 应用到其它选中项 (组拖动),
        // 恰好在 relayout 重置它们位置之后覆盖回来.
        emit dragMovedBy(this, delta);
        event->accept();
        return;
    }
    QGraphicsObject::mouseMoveEvent(event);
}

void BlockGraphicsItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        const bool wasPending = m_pendingRemove;
        m_dragging      = false;
        m_pendingRemove = false;
        update();
        emit dragEnded(this, wasPending);
    }
    QGraphicsObject::mouseReleaseEvent(event);
}

void BlockGraphicsItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
    emit requestEdit(this);
    QGraphicsObject::mouseDoubleClickEvent(event);
}

void BlockGraphicsItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event) {
    // contextMenuEvent 自动适配三平台:
    //   macOS: 双指 trackpad / Ctrl+单击 / 右键鼠标
    //   Windows / Linux: 鼠标右键
    QMenu menu;
    auto *editAct = menu.addAction(QObject::tr("编辑..."));
    QObject::connect(editAct, &QAction::triggered, [this]() { emit requestEdit(this); });

    auto *dupAct = menu.addAction(QObject::tr("复制"));
    dupAct->setShortcut(QKeySequence(QObject::tr("Ctrl+D")));  // Mac 自动转 Cmd+D
    QObject::connect(dupAct, &QAction::triggered, [this]() { emit requestDuplicate(this); });

    if (m_block && m_block->type() == BlockType::Quantifier) {
        auto *q = static_cast<QuantifierBlock*>(m_block.get());
        auto *linkAct = menu.addAction(QObject::tr("拖动时跟随前一项"));
        linkAct->setCheckable(true);
        linkAct->setChecked(q->linkedToPrevForDrag());
        QObject::connect(linkAct, &QAction::toggled, [this](bool checked) {
            emit requestToggleQuantifierLink(this, checked);
        });
    }

    menu.addSeparator();

    auto *delAct = menu.addAction(QObject::tr("删除"));
    delAct->setShortcut(QKeySequence::Delete);  // 跨平台标准删除键
    QObject::connect(delAct, &QAction::triggered, [this]() { emit requestDelete(this); });

    menu.exec(event->screenPos());
    event->accept();
}

void BlockGraphicsItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event) {
    m_hovered = true;
    update();
    QGraphicsObject::hoverEnterEvent(event);
}

void BlockGraphicsItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event) {
    m_hovered = false;
    update();
    QGraphicsObject::hoverLeaveEvent(event);
}
