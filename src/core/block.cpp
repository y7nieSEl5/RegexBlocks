#include "core/block.h"

#include <QJsonArray>
#include <QRegularExpression>

// ===== Block 基类 ===========================================================

QJsonObject Block::toJson() const {
    QJsonObject obj;
    obj["type"] = typeKey(type());
    writeJson(obj);
    return obj;
}

std::unique_ptr<Block> Block::fromJson(const QJsonObject &json) {
    const auto t = typeFromKey(json.value("type").toString());
    auto blk = createDefault(t);
    if (blk) {
        blk->readJson(json);
    }
    return blk;
}

std::unique_ptr<Block> Block::createDefault(BlockType type) {
    switch (type) {
        case BlockType::Literal:     return std::make_unique<LiteralBlock>(QStringLiteral("a"));
        case BlockType::CharClass:   return std::make_unique<CharClassBlock>(QStringLiteral("\\d"));
        case BlockType::Quantifier:  return std::make_unique<QuantifierBlock>(0, -1);
        case BlockType::CharSet:     return std::make_unique<CharSetBlock>(QStringLiteral("a-z"));
        case BlockType::Anchor:      return std::make_unique<AnchorBlock>(QStringLiteral("^"));
        case BlockType::Alternation: return std::make_unique<AlternationBlock>();
        case BlockType::BackReference:return std::make_unique<BackReferenceBlock>(QStringLiteral("1"), false);
        case BlockType::Group:       return std::make_unique<GroupBlock>(false);
    }
    return nullptr;
}

QString Block::typeKey(BlockType t) {
    switch (t) {
        case BlockType::Literal:     return QStringLiteral("literal");
        case BlockType::CharClass:   return QStringLiteral("charClass");
        case BlockType::Quantifier:  return QStringLiteral("quantifier");
        case BlockType::CharSet:     return QStringLiteral("charSet");
        case BlockType::Anchor:      return QStringLiteral("anchor");
        case BlockType::Alternation: return QStringLiteral("alternation");
        case BlockType::BackReference:return QStringLiteral("backReference");
        case BlockType::Group:       return QStringLiteral("group");
    }
    return {};
}

BlockType Block::typeFromKey(const QString &key) {
    if (key == QLatin1String("literal"))     return BlockType::Literal;
    if (key == QLatin1String("charClass"))   return BlockType::CharClass;
    if (key == QLatin1String("quantifier"))  return BlockType::Quantifier;
    if (key == QLatin1String("charSet"))     return BlockType::CharSet;
    if (key == QLatin1String("anchor"))      return BlockType::Anchor;
    if (key == QLatin1String("alternation")) return BlockType::Alternation;
    if (key == QLatin1String("backReference")) return BlockType::BackReference;
    if (key == QLatin1String("group"))       return BlockType::Group;
    return BlockType::Literal; // 兜底
}

// ===== LiteralBlock =========================================================

QString LiteralBlock::toRegex() const {
    // 只转义真正的正则元字符 (不像 QRegularExpression::escape 那样把 - / : 等也转).
    // 这样输出更可读, 也保证 RegexParser::parse(compile(blocks)) 往返一致.
    static const QString meta = QStringLiteral("\\^$.|?*+()[]{}");
    QString out;
    out.reserve(m_value.size() + 4);
    for (QChar c : m_value) {
        if (meta.contains(c)) out.append(QChar('\\'));
        out.append(c);
    }
    return out;
}

QString LiteralBlock::displayText() const {
    if (m_value.isEmpty()) return QStringLiteral("\"\"");
    return QStringLiteral("\"%1\"").arg(m_value);
}

std::unique_ptr<Block> LiteralBlock::clone() const {
    return std::make_unique<LiteralBlock>(m_value);
}

void LiteralBlock::writeJson(QJsonObject &out) const {
    out["value"] = m_value;
}

void LiteralBlock::readJson(const QJsonObject &in) {
    m_value = in.value("value").toString();
}

// ===== CharClassBlock =======================================================

QString CharClassBlock::displayText() const {
    static const QHash<QString, QString> labels = {
        {"\\d", "数字 \\d"},
        {"\\D", "非数字 \\D"},
        {"\\w", "字母数字 \\w"},
        {"\\W", "非字母数字 \\W"},
        {"\\s", "空白 \\s"},
        {"\\S", "非空白 \\S"},
        {".",   "任意字符 ."},
        {"\\p{L}", "Unicode 字母 \\p{L}"},
        {"\\P{L}", "非 Unicode 字母 \\P{L}"},
        {"\\xFF", "十六进制转义 \\xFF"},
        {"\\u4E2D", "Unicode 转义 \\u4E2D"},
        {"\\u{FFFF}", "Unicode 转义 \\u{FFFF}"},
        {"\\cI", "控制字符转义 \\cI"},
        {"\\t", "制表符 \\t"},
        {"\\n", "换行符 \\n"},
        {"\\v", "垂直制表符 \\v"},
        {"\\f", "换页符 \\f"},
        {"\\r", "回车符 \\r"},
        {"\\0", "NUL 字符 \\0"},
        {"\\+", "转义字面量 \\+"},
        {"\\000", "八进制转义 \\000"},
    };
    const QString exact = labels.value(m_klass);
    if (!exact.isEmpty()) return exact;

    if (m_klass.startsWith(QStringLiteral("\\u{")) && m_klass.endsWith(QChar('}'))) {
        return QStringLiteral("Unicode 转义 %1").arg(m_klass);
    }
    if (m_klass.startsWith(QStringLiteral("\\c")) && m_klass.size() == 3) {
        return QStringLiteral("控制字符转义 %1").arg(m_klass);
    }
    return m_klass;
}

std::unique_ptr<Block> CharClassBlock::clone() const {
    return std::make_unique<CharClassBlock>(m_klass);
}

void CharClassBlock::writeJson(QJsonObject &out) const {
    out["klass"] = m_klass;
}

void CharClassBlock::readJson(const QJsonObject &in) {
    m_klass = in.value("klass").toString(QStringLiteral("\\d"));
}

// ===== QuantifierBlock ======================================================

QString QuantifierBlock::toRegex() const {
    QString out;
    if (m_min == 0 && m_max == 1)         out = QStringLiteral("?");
    else if (m_min == 0 && m_max == -1)   out = QStringLiteral("*");
    else if (m_min == 1 && m_max == -1)   out = QStringLiteral("+");
    else if (m_min == m_max)              out = QStringLiteral("{%1}").arg(m_min);
    else if (m_max == -1)                 out = QStringLiteral("{%1,}").arg(m_min);
    else                                  out = QStringLiteral("{%1,%2}").arg(m_min).arg(m_max);
    if (m_lazy) out += QChar('?');
    return out;
}

QString QuantifierBlock::displayText() const {
    QString r = toRegex();
    return QStringLiteral("重复 %1").arg(r);
}

std::unique_ptr<Block> QuantifierBlock::clone() const {
    return std::make_unique<QuantifierBlock>(m_min, m_max, m_lazy, m_linkedToPrevForDrag);
}

void QuantifierBlock::writeJson(QJsonObject &out) const {
    out["min"]  = m_min;
    out["max"]  = m_max;
    out["lazy"] = m_lazy;
    out["linkDrag"] = m_linkedToPrevForDrag;
}

void QuantifierBlock::readJson(const QJsonObject &in) {
    m_min  = in.value("min").toInt(0);
    m_max  = in.value("max").toInt(-1);
    m_lazy = in.value("lazy").toBool(false);
    m_linkedToPrevForDrag = in.value("linkDrag").toBool(true);
}

// ===== CharSetBlock =========================================================

QString CharSetBlock::toRegex() const {
    return QStringLiteral("[%1%2]").arg(m_negated ? "^" : "", m_chars);
}

QString CharSetBlock::displayText() const {
    return QStringLiteral("[%1%2]").arg(m_negated ? "^" : "", m_chars);
}

std::unique_ptr<Block> CharSetBlock::clone() const {
    return std::make_unique<CharSetBlock>(m_chars, m_negated);
}

void CharSetBlock::writeJson(QJsonObject &out) const {
    out["chars"]   = m_chars;
    out["negated"] = m_negated;
}

void CharSetBlock::readJson(const QJsonObject &in) {
    m_chars   = in.value("chars").toString(QStringLiteral("a-z"));
    m_negated = in.value("negated").toBool(false);
}

// ===== AnchorBlock ==========================================================

QString AnchorBlock::displayText() const {
    static const QHash<QString, QString> labels = {
        {"^",   "行首 ^"},
        {"$",   "行尾 $"},
        {"\\b", "词边界 \\b"},
        {"\\B", "非词边界 \\B"},
    };
    return labels.value(m_anchor, m_anchor);
}

std::unique_ptr<Block> AnchorBlock::clone() const {
    return std::make_unique<AnchorBlock>(m_anchor);
}

void AnchorBlock::writeJson(QJsonObject &out) const {
    out["anchor"] = m_anchor;
}

void AnchorBlock::readJson(const QJsonObject &in) {
    m_anchor = in.value("anchor").toString(QStringLiteral("^"));
}

// ===== AlternationBlock =====================================================

std::unique_ptr<Block> AlternationBlock::clone() const {
    return std::make_unique<AlternationBlock>();
}

// ===== BackReferenceBlock ===================================================

QString BackReferenceBlock::toRegex() const {
    if (m_named) {
        return QStringLiteral("\\k<%1>").arg(m_ref);
    }
    return QStringLiteral("\\%1").arg(m_ref);
}

QString BackReferenceBlock::displayText() const {
    if (m_named) {
        return QStringLiteral("命名引用 \\k<%1>").arg(m_ref);
    }
    return QStringLiteral("数字引用 \\%1").arg(m_ref);
}

std::unique_ptr<Block> BackReferenceBlock::clone() const {
    return std::make_unique<BackReferenceBlock>(m_ref, m_named);
}

void BackReferenceBlock::writeJson(QJsonObject &out) const {
    out["ref"] = m_ref;
    out["named"] = m_named;
}

void BackReferenceBlock::readJson(const QJsonObject &in) {
    m_ref = in.value("ref").toString(QStringLiteral("1"));
    m_named = in.value("named").toBool(false);
}

// ===== GroupBlock ===========================================================

QString GroupBlock::toRegex() const {
    QString inner;
    for (const auto &c : m_children) {
        if (c) inner += c->toRegex();
    }
    if (!m_capturing) {
        return QStringLiteral("(?:%1)").arg(inner);
    }
    if (!m_name.isEmpty()) {
        return QStringLiteral("(?<%1>%2)").arg(m_name, inner);
    }
    return QStringLiteral("(%1)").arg(inner);
}

QString GroupBlock::displayText() const {
    // 顶部标签: 仅显示 ( 或 (?:; 子积木自己渲染, 不在父文字里重复
    if (!m_capturing) return QStringLiteral("(?:");
    if (!m_name.isEmpty()) return QStringLiteral("(?<%1>").arg(m_name);
    return QStringLiteral("(");
}

std::unique_ptr<Block> GroupBlock::clone() const {
    auto out = std::make_unique<GroupBlock>(m_capturing, m_name);
    for (const auto &c : m_children) {
        if (c) out->m_children.push_back(c->clone());
    }
    return out;
}

void GroupBlock::writeJson(QJsonObject &out) const {
    out["capturing"] = m_capturing;
    out["name"] = m_name;
    QJsonArray arr;
    for (const auto &c : m_children) {
        if (c) arr.append(c->toJson());
    }
    out["children"] = arr;
}

void GroupBlock::readJson(const QJsonObject &in) {
    m_capturing = in.value("capturing").toBool(false);
    m_name = in.value("name").toString();
    if (!m_capturing) m_name.clear();
    m_children.clear();
    const QJsonArray arr = in.value("children").toArray();
    for (const auto &v : arr) {
        if (v.isObject()) {
            auto child = Block::fromJson(v.toObject());
            if (child) m_children.push_back(std::move(child));
        }
    }
}
