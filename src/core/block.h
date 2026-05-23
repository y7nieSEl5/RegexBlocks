#pragma once

#include <QString>
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <memory>
#include <vector>

// 所有积木类型
enum class BlockType {
    Literal,       // 字面量 -> "abc"  (转义后)
    CharClass,     // \d \w \s . \D \W \S
    Quantifier,    // ? * + {n} {n,m}
    CharSet,       // [abc] [a-z] [^...]
    Anchor,        // ^ $ \b \B
    Alternation,   // |
    BackReference, // \1 / \k<name>
    Group,         // (...) / (?:...)  Scratch 风格容器, 子积木物理嵌入
};

// 积木数据基类 (纯数据, 不含渲染)
class Block {
public:
    virtual ~Block() = default;

    virtual BlockType type() const = 0;
    virtual QString   toRegex() const = 0;       // 输出正则片段
    virtual QString   displayText() const = 0;   // 积木上的文字
    virtual QString   category() const = 0;      // 用于左侧分组
    virtual QColor    color() const = 0;         // 颜色编码

    // 序列化 - 子类只填充 type 字段以外的数据
    QJsonObject toJson() const;
    virtual std::unique_ptr<Block> clone() const = 0;

    // 工厂: JSON -> Block
    static std::unique_ptr<Block> fromJson(const QJsonObject &json);
    // 工厂: 类型 -> 默认实例
    static std::unique_ptr<Block> createDefault(BlockType type);

protected:
    virtual void writeJson(QJsonObject &out) const = 0;
    virtual void readJson(const QJsonObject &in) = 0;

private:
    static QString typeKey(BlockType t);
    static BlockType typeFromKey(const QString &key);
};

// ----- 具体子类 -------------------------------------------------------------

class LiteralBlock : public Block {
public:
    LiteralBlock() = default;
    explicit LiteralBlock(QString value) : m_value(std::move(value)) {}

    BlockType type() const override { return BlockType::Literal; }
    QString   toRegex() const override;
    QString   displayText() const override;
    QString   category() const override { return QStringLiteral("字面量"); }
    QColor    color() const override   { return QColor("#7CB342"); } // 绿
    std::unique_ptr<Block> clone() const override;

    const QString &value() const { return m_value; }
    void setValue(const QString &v) { m_value = v; }

protected:
    void writeJson(QJsonObject &out) const override;
    void readJson(const QJsonObject &in) override;

private:
    QString m_value;
};

class CharClassBlock : public Block {
public:
    CharClassBlock() = default;
    // klass: "\\d" "\\w" "\\s" "." "\\D" "\\W" "\\S" "\\t" "\\n" "\\u{FFFF}" ...
    explicit CharClassBlock(QString klass) : m_klass(std::move(klass)) {}

    BlockType type() const override { return BlockType::CharClass; }
    QString   toRegex() const override     { return m_klass; }
    QString   displayText() const override;
    QString   category() const override { return QStringLiteral("字符类"); }
    QColor    color() const override   { return QColor("#42A5F5"); } // 蓝
    std::unique_ptr<Block> clone() const override;

    const QString &klass() const { return m_klass; }
    void setKlass(const QString &k) { m_klass = k; }

protected:
    void writeJson(QJsonObject &out) const override;
    void readJson(const QJsonObject &in) override;

private:
    QString m_klass = QStringLiteral("\\d");
};

class QuantifierBlock : public Block {
public:
    QuantifierBlock() = default;
    QuantifierBlock(int min, int max, bool lazy = false, bool linkedToPrevForDrag = true)
        : m_min(min), m_max(max), m_lazy(lazy),
          m_linkedToPrevForDrag(linkedToPrevForDrag) {}

    BlockType type() const override { return BlockType::Quantifier; }
    QString   toRegex() const override;     // 返回 ? * + {n} {n,m}
    QString   displayText() const override;
    QString   category() const override { return QStringLiteral("量词"); }
    QColor    color() const override   { return QColor("#FFA726"); } // 橙
    std::unique_ptr<Block> clone() const override;

    int  min() const  { return m_min; }
    int  max() const  { return m_max; }   // -1 表示无穷
    bool lazy() const { return m_lazy; }
    bool linkedToPrevForDrag() const { return m_linkedToPrevForDrag; }
    void setRange(int mn, int mx) { m_min = mn; m_max = mx; }
    void setLazy(bool lazy)       { m_lazy = lazy; }
    void setLinkedToPrevForDrag(bool v) { m_linkedToPrevForDrag = v; }

protected:
    void writeJson(QJsonObject &out) const override;
    void readJson(const QJsonObject &in) override;

private:
    int  m_min = 0;
    int  m_max = -1;     // -1 = 无上限
    bool m_lazy = false;
    bool m_linkedToPrevForDrag = true; // UX: 拖动前一项时是否自动跟随
};

class CharSetBlock : public Block {
public:
    CharSetBlock() = default;
    explicit CharSetBlock(QString chars, bool negated = false)
        : m_chars(std::move(chars)), m_negated(negated) {}

    BlockType type() const override { return BlockType::CharSet; }
    QString   toRegex() const override;     // [abc] 或 [^abc]
    QString   displayText() const override;
    QString   category() const override { return QStringLiteral("字符集"); }
    QColor    color() const override   { return QColor("#26A69A"); } // 青
    std::unique_ptr<Block> clone() const override;

    const QString &chars() const { return m_chars; }
    bool negated() const         { return m_negated; }
    void setChars(const QString &c) { m_chars = c; }
    void setNegated(bool n)         { m_negated = n; }

protected:
    void writeJson(QJsonObject &out) const override;
    void readJson(const QJsonObject &in) override;

private:
    QString m_chars   = QStringLiteral("a-z");
    bool    m_negated = false;
};

class AnchorBlock : public Block {
public:
    AnchorBlock() = default;
    explicit AnchorBlock(QString anchor) : m_anchor(std::move(anchor)) {}

    BlockType type() const override { return BlockType::Anchor; }
    QString   toRegex() const override     { return m_anchor; }
    QString   displayText() const override;
    QString   category() const override { return QStringLiteral("锚点"); }
    QColor    color() const override   { return QColor("#AB47BC"); } // 紫
    std::unique_ptr<Block> clone() const override;

    const QString &anchor() const { return m_anchor; }
    void setAnchor(const QString &a) { m_anchor = a; }

protected:
    void writeJson(QJsonObject &out) const override;
    void readJson(const QJsonObject &in) override;

private:
    QString m_anchor = QStringLiteral("^");
};

class AlternationBlock : public Block {
public:
    AlternationBlock() = default;

    BlockType type() const override { return BlockType::Alternation; }
    QString   toRegex() const override     { return QStringLiteral("|"); }
    QString   displayText() const override { return QStringLiteral("或 |"); }
    QString   category() const override { return QStringLiteral("逻辑"); }
    QColor    color() const override   { return QColor("#EC407A"); } // 粉
    std::unique_ptr<Block> clone() const override;

protected:
    void writeJson(QJsonObject &out) const override { Q_UNUSED(out) }
    void readJson(const QJsonObject &in) override   { Q_UNUSED(in) }
};

class BackReferenceBlock : public Block {
public:
    BackReferenceBlock() = default;
    BackReferenceBlock(QString ref, bool named = false)
        : m_ref(std::move(ref)), m_named(named) {}

    BlockType type() const override { return BlockType::BackReference; }
    QString   toRegex() const override;
    QString   displayText() const override;
    QString   category() const override { return QStringLiteral("引用"); }
    QColor    color() const override   { return QColor("#8D6E63"); } // 棕
    std::unique_ptr<Block> clone() const override;

    const QString &ref() const { return m_ref; }
    bool named() const         { return m_named; }
    void setRef(const QString &r) { m_ref = r; }
    void setNamed(bool n)         { m_named = n; }

protected:
    void writeJson(QJsonObject &out) const override;
    void readJson(const QJsonObject &in) override;

private:
    QString m_ref = QStringLiteral("1");
    bool    m_named = false;
};

// Scratch 风格容器: 子积木物理嵌入, toRegex 递归输出 (...) 或 (?:...)
class GroupBlock : public Block {
public:
    GroupBlock() = default;
    explicit GroupBlock(bool capturing, QString name = {})
        : m_capturing(capturing), m_name(std::move(name)) {}

    BlockType type() const override { return BlockType::Group; }
    QString   toRegex() const override;
    QString   displayText() const override;   // 顶部标签 (?: 或 (
    QString   category() const override { return QStringLiteral("分组"); }
    QColor    color() const override   { return QColor("#FFC107"); } // 琥珀
    std::unique_ptr<Block> clone() const override;

    bool capturing() const          { return m_capturing; }
    void setCapturing(bool c) {
        m_capturing = c;
        if (!m_capturing) m_name.clear();
    }
    const QString &name() const     { return m_name; }
    void setName(const QString &n) {
        m_name = n.trimmed();
        if (!m_name.isEmpty()) m_capturing = true;
    }

    // 子积木访问 / 修改 (BlockCanvas 直接操作).
    // 用 std::vector (而不是 QList) 因为 QList 在 reallocate 时会尝试拷贝构造,
    // 而 unique_ptr 是 move-only.
    const std::vector<std::unique_ptr<Block>>& children() const { return m_children; }
    std::vector<std::unique_ptr<Block>>&       children()       { return m_children; }

protected:
    void writeJson(QJsonObject &out) const override;
    void readJson(const QJsonObject &in) override;

private:
    bool m_capturing = false;
    QString m_name;
    std::vector<std::unique_ptr<Block>> m_children;
};
