#include "ui/blockpalette.h"

#include "core/block.h"
#include "ui/theme.h"
#include "util/fonts.h"

#include <QListWidgetItem>
#include <QDrag>
#include <QMimeData>
#include <QJsonDocument>
#include <QPainter>
#include <QPixmap>
#include <QFontMetrics>

const char *BlockPalette::kMimeType = "application/x-regex-block";

namespace {
constexpr int kItemRoleProto = Qt::UserRole + 1;  // QString JSON
constexpr int kItemRoleColor = Qt::UserRole + 2;  // QColor
constexpr int kItemRoleHeader = Qt::UserRole + 3; // bool

QPixmap renderBlockPixmap(const QString &text, const QColor &color) {
    QFont f = ui::uiFont(12, /*bold=*/true);
    QFontMetrics fm(f);
    const int padX = 14;
    const int h = 36;
    const int w = std::max(80, fm.horizontalAdvance(text) + padX * 2);

    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient grad(0, 0, 0, h);
    grad.setColorAt(0.0, color.lighter(115));
    grad.setColorAt(1.0, color);
    p.setBrush(grad);
    p.setPen(QPen(color.darker(125), 1.2));
    p.drawRoundedRect(QRectF(0.5, 0.5, w - 1, h - 1), 8, 8);
    p.setPen(Qt::white);
    p.setFont(f);
    p.drawText(pix.rect(), Qt::AlignCenter, text);
    return pix;
}
} // namespace

BlockPalette::BlockPalette(QWidget *parent)
    : QListWidget(parent) {
    setMinimumWidth(220);
    setIconSize(QSize(160, 36));
    setSpacing(2);
    setDragEnabled(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    applyThemedStyle();
    populate();

    // 主题切换 -> 重新生成 QSS + 刷新分类条颜色
    connect(&Theme::instance(), &Theme::themeChanged, this, [this]() {
        applyThemedStyle();
        // 分类条 / 文字色是 setBackground/setForeground 设置的, 需要逐项更新
        for (int i = 0; i < count(); ++i) {
            auto *it = item(i);
            if (it && it->data(kItemRoleHeader).toBool()) {
                it->setForeground(Theme::instance().categoryHeaderFg());
                it->setBackground(Theme::instance().categoryHeaderBg());
            }
        }
        viewport()->update();
    });
}

void BlockPalette::applyThemedStyle() {
    auto &t = Theme::instance();
    setStyleSheet(QString(
        "QListWidget { background: %1; border: none; color: %2; outline: none; }"
        "QListWidget::item { padding: 3px; }"
        "QListWidget::item:selected { background: transparent; color: %2; }"
    ).arg(t.panelAlt().name(),
          t.textPrimary().name()));
}

void BlockPalette::addCategoryHeader(const QString &title) {
    auto *item = new QListWidgetItem(title);
    item->setFlags(Qt::ItemIsEnabled); // 不可选中, 不可拖拽
    QFont f = item->font();
    f.setBold(true);
    f.setPointSize(f.pointSize() + 1);
    item->setFont(f);
    item->setForeground(Theme::instance().categoryHeaderFg());
    item->setBackground(Theme::instance().categoryHeaderBg());
    item->setData(kItemRoleHeader, true);
    addItem(item);
}

void BlockPalette::addBlockItem(std::unique_ptr<Block> prototype) {
    if (!prototype) return;
    const QJsonDocument doc(prototype->toJson());
    const QString json = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    const QString text = prototype->displayText();
    const QColor  col  = prototype->color();

    auto *item = new QListWidgetItem(text);
    item->setIcon(QIcon(renderBlockPixmap(text, col)));
    item->setData(kItemRoleProto, json);
    item->setData(kItemRoleColor, col);
    item->setData(Qt::ToolTipRole,
                  QStringLiteral("正则: %1").arg(prototype->toRegex()));
    item->setText(QString()); // 用 icon 显示, 文字留空更整洁
    item->setSizeHint(QSize(180, 42));
    addItem(item);
}

void BlockPalette::populate() {
    addCategoryHeader(tr("字符类"));
    for (const auto &k : {QStringLiteral("."),
                          QStringLiteral("\\d"), QStringLiteral("\\D"),
                          QStringLiteral("\\w"), QStringLiteral("\\W"),
                          QStringLiteral("\\s"), QStringLiteral("\\S"),
                          QStringLiteral("\\p{L}"), QStringLiteral("\\P{L}")}) {
        addBlockItem(std::make_unique<CharClassBlock>(k));
    }

    addCategoryHeader(tr("量词"));
    addBlockItem(std::make_unique<QuantifierBlock>(0, 1));      // ?
    addBlockItem(std::make_unique<QuantifierBlock>(0, -1));     // *
    addBlockItem(std::make_unique<QuantifierBlock>(1, -1));     // +
    addBlockItem(std::make_unique<QuantifierBlock>(3, 3));      // {3}
    addBlockItem(std::make_unique<QuantifierBlock>(1, 5));      // {1,5}

    addCategoryHeader(tr("字符集"));
    addBlockItem(std::make_unique<CharSetBlock>("a-z"));
    addBlockItem(std::make_unique<CharSetBlock>("A-Z"));
    addBlockItem(std::make_unique<CharSetBlock>("0-9"));
    addBlockItem(std::make_unique<CharSetBlock>("a-zA-Z0-9"));
    addBlockItem(std::make_unique<CharSetBlock>("\\s\\S")); // [\s\S] 任意字符 (含换行)

    addCategoryHeader(tr("锚点"));
    addBlockItem(std::make_unique<AnchorBlock>("^"));
    addBlockItem(std::make_unique<AnchorBlock>("$"));
    addBlockItem(std::make_unique<AnchorBlock>("\\b"));
    addBlockItem(std::make_unique<AnchorBlock>("\\B"));

    addCategoryHeader(tr("逻辑"));
    addBlockItem(std::make_unique<AlternationBlock>());

    addCategoryHeader(tr("分组 (可嵌套子积木)"));
    addBlockItem(std::make_unique<GroupBlock>(/*capturing=*/false));  // (?:...)
    addBlockItem(std::make_unique<GroupBlock>(/*capturing=*/true));   // (...)
    addBlockItem(std::make_unique<GroupBlock>(/*capturing=*/true, "name"));   // (?<name>...)

    addCategoryHeader(tr("引用"));
    addBlockItem(std::make_unique<BackReferenceBlock>("1", false));        // \1
    addBlockItem(std::make_unique<BackReferenceBlock>("name", true));      // \k<name>

    addCategoryHeader(tr("字面量"));
    addBlockItem(std::make_unique<LiteralBlock>("abc"));
    addBlockItem(std::make_unique<LiteralBlock>("-"));
    addBlockItem(std::make_unique<LiteralBlock>("@"));

    addCategoryHeader(tr("转义字符"));
    addBlockItem(std::make_unique<CharClassBlock>("\\+"));
    addBlockItem(std::make_unique<CharClassBlock>("\\t"));
    addBlockItem(std::make_unique<CharClassBlock>("\\n"));
    addBlockItem(std::make_unique<CharClassBlock>("\\v"));
    addBlockItem(std::make_unique<CharClassBlock>("\\f"));
    addBlockItem(std::make_unique<CharClassBlock>("\\r"));
    addBlockItem(std::make_unique<CharClassBlock>("\\0"));
    addBlockItem(std::make_unique<CharClassBlock>("\\cI"));
    addBlockItem(std::make_unique<CharClassBlock>("\\000"));
    addBlockItem(std::make_unique<CharClassBlock>("\\xFF"));
    addBlockItem(std::make_unique<CharClassBlock>("\\u4E2D"));
    addBlockItem(std::make_unique<CharClassBlock>("\\u{FFFF}"));
}

void BlockPalette::startDrag(Qt::DropActions /*supportedActions*/) {
    auto *item = currentItem();
    if (!item) return;
    if (item->data(kItemRoleHeader).toBool()) return;
    const QString json = item->data(kItemRoleProto).toString();
    if (json.isEmpty()) return;

    auto *mime = new QMimeData;
    mime->setData(kMimeType, json.toUtf8());

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);

    // 用积木图像作为拖拽预览
    if (!item->icon().isNull()) {
        const QPixmap pix = item->icon().pixmap(item->icon().actualSize(QSize(220, 56)));
        drag->setPixmap(pix);
        drag->setHotSpot(QPoint(pix.width() / 2, pix.height() / 2));
    }

    drag->exec(Qt::CopyAction);
}
