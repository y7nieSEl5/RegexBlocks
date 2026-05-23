#include "ui/blockeditor.h"

#include "core/block.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QPalette>
#include <QFont>
#include <QMessageBox>
#include <QRegularExpression>

namespace {

bool editLiteral(LiteralBlock *blk, QWidget *parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("编辑字面量"));
    auto *form = new QFormLayout;
    auto *edit = new QLineEdit(blk->value());
    edit->setPlaceholderText(QObject::tr("输入字面文本 (会自动转义)"));
    form->addRow(QObject::tr("文本:"), edit);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *root = new QVBoxLayout(&dlg);
    root->addLayout(form);
    root->addWidget(bb);

    if (dlg.exec() != QDialog::Accepted) return false;
    blk->setValue(edit->text());
    return true;
}

bool editCharClass(CharClassBlock *blk, QWidget *parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("编辑字符类"));
    auto *form = new QFormLayout;
    auto *combo = new QComboBox;
    combo->setEditable(true);
    struct Opt { QString value; QString label; };
    const QVector<Opt> opts = {
        {"\\d", "\\d  数字"},      {"\\D", "\\D  非数字"},
        {"\\w", "\\w  字母数字"},  {"\\W", "\\W  非字母数字"},
        {"\\s", "\\s  空白"},      {"\\S", "\\S  非空白"},
        {".",   ".   任意字符"},
        {"\\p{L}", "\\p{L}  Unicode 字母"},
        {"\\P{L}", "\\P{L}  非 Unicode 字母"},
        {"\\t", "\\t  制表符"},
        {"\\n", "\\n  换行符"},
        {"\\v", "\\v  垂直制表符"},
        {"\\f", "\\f  换页符"},
        {"\\r", "\\r  回车符"},
        {"\\0", "\\0  NUL 字符"},
        {"\\cI", "\\cI  控制字符转义"},
        {"\\xFF", "\\xFF  十六进制转义"},
        {"\\u4E2D", "\\u4E2D  Unicode 转义"},
        {"\\u{FFFF}", "\\u{FFFF}  Unicode 转义 (花括号)"},
        {"\\+", "\\+  转义字面量 +"},
        {"\\000", "\\000  八进制转义"},
    };
    int idx = 0;
    for (int i = 0; i < opts.size(); ++i) {
        combo->addItem(opts[i].label, opts[i].value);
        if (opts[i].value == blk->klass()) idx = i;
    }
    combo->addItem(QObject::tr("自定义 (可直接输入)"), QString());
    if (idx > 0 || blk->klass() == opts[0].value) {
        combo->setCurrentIndex(idx);
    } else {
        combo->setCurrentIndex(combo->count() - 1);
        combo->setEditText(blk->klass());
    }
    form->addRow(QObject::tr("类型 (可编辑):"), combo);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *root = new QVBoxLayout(&dlg);
    root->addLayout(form);
    root->addWidget(bb);

    if (dlg.exec() != QDialog::Accepted) return false;
    QString value = combo->currentData().toString();
    if (value.isEmpty()) value = combo->currentText().trimmed();
    if (value.isEmpty()) value = QStringLiteral("\\d");
    blk->setKlass(value);
    return true;
}

bool editQuantifier(QuantifierBlock *blk, QWidget *parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("编辑量词"));
    auto *form = new QFormLayout;

    auto *minSpin = new QSpinBox;
    minSpin->setRange(0, 9999);
    minSpin->setValue(blk->min());

    auto *maxSpin = new QSpinBox;
    maxSpin->setRange(-1, 9999);
    maxSpin->setSpecialValueText(QObject::tr("无穷"));
    maxSpin->setValue(blk->max());

    auto *lazy = new QCheckBox(QObject::tr("非贪婪 (lazy)"));
    lazy->setChecked(blk->lazy());
    auto *follow = new QCheckBox(QObject::tr("拖动被修饰项时一起跟随"));
    follow->setChecked(blk->linkedToPrevForDrag());

    auto *hint = new QLabel(QObject::tr(
        "提示:\n"
        "  min=0, max=1   -> ?\n"
        "  min=0, max=-1  -> *  (-1 表示无穷)\n"
        "  min=1, max=-1  -> +\n"
        "  min=3, max=3   -> {3}\n"
        "  min=1, max=5   -> {1,5}\n"
        "  取消勾选\"一起跟随\"后, 该量词仍参与正则, 但不会被主块拖拽带走."));
    {
        QPalette pp = hint->palette();
        pp.setColor(QPalette::WindowText, pp.color(QPalette::PlaceholderText));
        hint->setPalette(pp);
        QFont hf = hint->font();
        hf.setPointSizeF(hf.pointSizeF() - 1.0);
        hint->setFont(hf);
    }

    form->addRow(QObject::tr("最少:"), minSpin);
    form->addRow(QObject::tr("最多:"), maxSpin);
    form->addRow(QString(), lazy);
    form->addRow(QString(), follow);
    form->addRow(QString(), hint);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *root = new QVBoxLayout(&dlg);
    root->addLayout(form);
    root->addWidget(bb);

    if (dlg.exec() != QDialog::Accepted) return false;
    blk->setRange(minSpin->value(), maxSpin->value());
    blk->setLazy(lazy->isChecked());
    blk->setLinkedToPrevForDrag(follow->isChecked());
    return true;
}

bool editCharSet(CharSetBlock *blk, QWidget *parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("编辑字符集"));
    auto *form = new QFormLayout;
    auto *edit = new QLineEdit(blk->chars());
    edit->setPlaceholderText(QObject::tr("例: a-z 或 abc 或 0-9A-F"));
    auto *neg = new QCheckBox(QObject::tr("取反 [^...]"));
    neg->setChecked(blk->negated());
    form->addRow(QObject::tr("字符:"), edit);
    form->addRow(QString(), neg);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *root = new QVBoxLayout(&dlg);
    root->addLayout(form);
    root->addWidget(bb);

    if (dlg.exec() != QDialog::Accepted) return false;
    blk->setChars(edit->text());
    blk->setNegated(neg->isChecked());
    return true;
}

bool editGroup(GroupBlock *blk, QWidget *parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("编辑分组"));
    auto *form = new QFormLayout;
    auto *cap = new QCheckBox(QObject::tr("捕获分组 (...) , 取消勾选则为非捕获 (?:...)"));
    cap->setChecked(blk->capturing());
    auto *nameEdit = new QLineEdit(blk->name());
    nameEdit->setPlaceholderText(QObject::tr("可选: 命名捕获组, 例如 word / id"));
    nameEdit->setEnabled(cap->isChecked());
    QObject::connect(cap, &QCheckBox::toggled, nameEdit, &QLineEdit::setEnabled);

    auto *hint = new QLabel(QObject::tr(
        "捕获分组 (...)  : 匹配的文本可通过 \\1 / $1 反向引用.\n"
        "命名捕获 (?<name>...) : 可通过 \\k<name> 引用.\n"
        "非捕获 (?:...) : 仅作分组逻辑, 不占用捕获组编号, 性能略好.\n"
        "\n"
        "提示: 把别的积木拖到这个分组的内部 → 物理嵌入 (Scratch 风格)."));
    {
        QPalette pp = hint->palette();
        pp.setColor(QPalette::WindowText, pp.color(QPalette::PlaceholderText));
        hint->setPalette(pp);
        QFont hf = hint->font();
        hf.setPointSizeF(hf.pointSizeF() - 1.0);
        hint->setFont(hf);
        hint->setWordWrap(true);
    }
    form->addRow(QString(), cap);
    form->addRow(QObject::tr("分组名 (可选):"), nameEdit);
    form->addRow(QString(), hint);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *root = new QVBoxLayout(&dlg);
    root->addLayout(form);
    root->addWidget(bb);

    const QRegularExpression nameRe(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    while (true) {
        if (dlg.exec() != QDialog::Accepted) return false;
        const bool capturing = cap->isChecked();
        const QString name = nameEdit->text().trimmed();
        if (capturing && !name.isEmpty() && !nameRe.match(name).hasMatch()) {
            QMessageBox::warning(&dlg, QObject::tr("名称不合法"),
                                 QObject::tr("命名分组名只能由字母/数字/下划线组成, 且不能以数字开头。"));
            continue;
        }
        blk->setCapturing(capturing);
        blk->setName(capturing ? name : QString());
        return true;
    }
}

bool editBackReference(BackReferenceBlock *blk, QWidget *parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("编辑引用"));
    auto *form = new QFormLayout;

    auto *mode = new QComboBox;
    mode->addItem(QObject::tr("数字引用 (\\1 / \\2 ...)"), false);
    mode->addItem(QObject::tr("命名引用 (\\k<name>)"), true);
    mode->setCurrentIndex(blk->named() ? 1 : 0);

    auto *refEdit = new QLineEdit(blk->ref());
    refEdit->setPlaceholderText(QObject::tr("数字模式填 1/2..., 命名模式填 name"));

    form->addRow(QObject::tr("模式:"), mode);
    form->addRow(QObject::tr("引用目标:"), refEdit);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *root = new QVBoxLayout(&dlg);
    root->addLayout(form);
    root->addWidget(bb);

    const QRegularExpression numRe(QStringLiteral("^[1-9][0-9]*$"));
    const QRegularExpression nameRe(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    while (true) {
        if (dlg.exec() != QDialog::Accepted) return false;
        const bool named = mode->currentData().toBool();
        const QString ref = refEdit->text().trimmed();
        const bool ok = named ? nameRe.match(ref).hasMatch() : numRe.match(ref).hasMatch();
        if (!ok) {
            QMessageBox::warning(&dlg, QObject::tr("引用不合法"),
                                 named
                                     ? QObject::tr("命名引用必须是合法标识符 (如 word, user_id)。")
                                     : QObject::tr("数字引用必须是正整数 (如 1, 2, 12)。"));
            continue;
        }
        blk->setNamed(named);
        blk->setRef(ref);
        return true;
    }
}

bool editAnchor(AnchorBlock *blk, QWidget *parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("编辑锚点"));
    auto *form = new QFormLayout;
    auto *combo = new QComboBox;
    struct Opt { QString value; QString label; };
    const QVector<Opt> opts = {
        {"^",   "^   行首"},
        {"$",   "$   行尾"},
        {"\\b", "\\b  词边界"},
        {"\\B", "\\B  非词边界"},
    };
    int idx = 0;
    for (int i = 0; i < opts.size(); ++i) {
        combo->addItem(opts[i].label, opts[i].value);
        if (opts[i].value == blk->anchor()) idx = i;
    }
    combo->setCurrentIndex(idx);
    form->addRow(QObject::tr("锚点:"), combo);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *root = new QVBoxLayout(&dlg);
    root->addLayout(form);
    root->addWidget(bb);

    if (dlg.exec() != QDialog::Accepted) return false;
    blk->setAnchor(combo->currentData().toString());
    return true;
}

} // namespace

bool BlockEditor::edit(Block *block, QWidget *parent) {
    if (!block) return false;
    switch (block->type()) {
        case BlockType::Literal:    return editLiteral(static_cast<LiteralBlock*>(block), parent);
        case BlockType::CharClass:  return editCharClass(static_cast<CharClassBlock*>(block), parent);
        case BlockType::Quantifier: return editQuantifier(static_cast<QuantifierBlock*>(block), parent);
        case BlockType::CharSet:    return editCharSet(static_cast<CharSetBlock*>(block), parent);
        case BlockType::Anchor:     return editAnchor(static_cast<AnchorBlock*>(block), parent);
        case BlockType::Alternation: return false;  // 无可编辑参数
        case BlockType::BackReference:
            return editBackReference(static_cast<BackReferenceBlock*>(block), parent);
        case BlockType::Group:      return editGroup(static_cast<GroupBlock*>(block), parent);
    }
    return false;
}
