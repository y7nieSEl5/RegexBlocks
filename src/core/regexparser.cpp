#include "core/regexparser.h"

#include "core/block.h"

#include <QChar>
#include <utility>

namespace {

bool isHexDigit(QChar c) {
    return (c >= QChar('0') && c <= QChar('9'))
        || (c >= QChar('a') && c <= QChar('f'))
        || (c >= QChar('A') && c <= QChar('F'));
}

bool isAsciiLetter(QChar c) {
    return (c >= QChar('a') && c <= QChar('z'))
        || (c >= QChar('A') && c <= QChar('Z'));
}

// 内部异常: 用于把"不支持的特性"沿递归栈快速反传到 parse() 顶层
struct ParseException {
    QString msg;
    int     pos;
};

class Parser {
public:
    explicit Parser(const QString &input) : m_in(input) {}

    std::vector<std::unique_ptr<Block>> parseTop() {
        // 顶层就是一个 alternation 序列
        auto seq = parseAlternation(/*topLevel=*/true);
        if (m_pos < m_in.size()) {
            throw ParseException{QStringLiteral("意外字符 '%1'").arg(m_in[m_pos]), m_pos};
        }
        return seq;
    }

private:
    // 解析一个 alternation: sequence ('|' sequence)*
    // 返回扁平 Block 列表, alternation 用 AlternationBlock 占位.
    std::vector<std::unique_ptr<Block>> parseAlternation(bool topLevel) {
        std::vector<std::unique_ptr<Block>> out;
        appendAll(out, parseSequence(topLevel));
        while (m_pos < m_in.size() && m_in[m_pos] == QChar('|')) {
            ++m_pos;
            out.push_back(std::make_unique<AlternationBlock>());
            appendAll(out, parseSequence(topLevel));
        }
        return out;
    }

    // 解析一个 sequence: atom*
    std::vector<std::unique_ptr<Block>> parseSequence(bool topLevel) {
        std::vector<std::unique_ptr<Block>> out;
        QString litBuffer;   // 累积连续非元字符 → 合并成一个 LiteralBlock
        auto flushLiteral = [&](bool keepLast) {
            if (litBuffer.isEmpty()) return;
            if (keepLast && litBuffer.size() > 1) {
                const QString head = litBuffer.left(litBuffer.size() - 1);
                const QString tail = litBuffer.right(1);
                out.push_back(std::make_unique<LiteralBlock>(head));
                out.push_back(std::make_unique<LiteralBlock>(tail));
            } else {
                out.push_back(std::make_unique<LiteralBlock>(litBuffer));
            }
            litBuffer.clear();
        };

        while (m_pos < m_in.size()) {
            const QChar c = m_in[m_pos];
            if (c == QChar('|') || c == QChar(')')) break;
            if (topLevel && c == QChar(']')) {
                throw ParseException{QStringLiteral("意外的 ']'"), m_pos};
            }

            auto isQuantifierAt = [&](int p) {
                if (p >= m_in.size()) return false;
                const QChar q = m_in[p];
                return q == QChar('?') || q == QChar('*')
                    || q == QChar('+') || q == QChar('{');
            };

            if (c == QChar('\\')) {
                if (m_pos + 1 >= m_in.size()) {
                    throw ParseException{QStringLiteral("末尾悬挂的 '\\'"), m_pos};
                }
                const QChar e = m_in[m_pos + 1];
                if (e == QChar('d') || e == QChar('D') || e == QChar('w') || e == QChar('W')
                    || e == QChar('s') || e == QChar('S')) {
                    flushLiteral(/*keepLast=*/false);
                    auto blk = std::make_unique<CharClassBlock>(QStringLiteral("\\%1").arg(e));
                    m_pos += 2;
                    maybeAttachQuantifier(out, std::move(blk));
                } else if (e == QChar('b') || e == QChar('B')) {
                    flushLiteral(/*keepLast=*/false);
                    out.push_back(std::make_unique<AnchorBlock>(QStringLiteral("\\%1").arg(e)));
                    m_pos += 2;
                } else if (e == QChar('p') || e == QChar('P')) {
                    int p = m_pos + 2;
                    if (p >= m_in.size() || m_in[p] != QChar('{')) {
                        throw ParseException{
                            QStringLiteral("Unicode 属性需要写成 \\%1{...}").arg(e), m_pos};
                    }
                    ++p; // skip '{'
                    QString prop;
                    while (p < m_in.size() && m_in[p] != QChar('}')) {
                        prop.append(m_in[p]);
                        ++p;
                    }
                    if (p >= m_in.size() || prop.isEmpty()) {
                        throw ParseException{QStringLiteral("Unicode 属性缺少 '}'"), m_pos};
                    }
                    ++p; // skip '}'
                    flushLiteral(/*keepLast=*/false);
                    auto blk = std::make_unique<CharClassBlock>(
                        QStringLiteral("\\%1{%2}").arg(e, prop));
                    m_pos = p;
                    maybeAttachQuantifier(out, std::move(blk));
                } else if (e == QChar('k')) {
                    int p = m_pos + 2;
                    if (p >= m_in.size() || m_in[p] != QChar('<')) {
                        throw ParseException{QStringLiteral("命名引用需要写成 \\k<name>"), m_pos};
                    }
                    ++p; // skip '<'
                    QString name;
                    while (p < m_in.size() && m_in[p] != QChar('>')) {
                        name.append(m_in[p]);
                        ++p;
                    }
                    if (p >= m_in.size() || name.isEmpty()) {
                        throw ParseException{QStringLiteral("命名引用缺少 '>'"), m_pos};
                    }
                    ++p; // skip '>'
                    flushLiteral(/*keepLast=*/false);
                    m_pos = p;
                    maybeAttachQuantifier(out, std::make_unique<BackReferenceBlock>(name, true));
                } else if (e == QChar('c')) {
                    if (m_pos + 2 >= m_in.size() || !isAsciiLetter(m_in[m_pos + 2])) {
                        throw ParseException{QStringLiteral("\\c 转义需要一个字母, 如 \\cI"), m_pos};
                    }
                    const QString token = QStringLiteral("\\c%1").arg(m_in[m_pos + 2]);
                    flushLiteral(/*keepLast=*/false);
                    m_pos += 3;
                    maybeAttachQuantifier(out, std::make_unique<CharClassBlock>(token));
                } else if (e == QChar('0')) {
                    // \0 / \000 八进制转义
                    int p = m_pos + 1;
                    QString oct;
                    while (p < m_in.size() && oct.size() < 3
                           && m_in[p] >= QChar('0') && m_in[p] <= QChar('7')) {
                        oct.append(m_in[p]);
                        ++p;
                    }
                    if (oct.isEmpty()) oct = QStringLiteral("0");
                    flushLiteral(/*keepLast=*/false);
                    m_pos = p;
                    auto blk = std::make_unique<CharClassBlock>(QStringLiteral("\\%1").arg(oct));
                    maybeAttachQuantifier(out, std::move(blk));
                } else if (e.isDigit()) {
                    // \1 / \12 数字反向引用
                    int p = m_pos + 1;
                    QString digits;
                    while (p < m_in.size() && m_in[p].isDigit()) {
                        digits.append(m_in[p]);
                        ++p;
                    }
                    flushLiteral(/*keepLast=*/false);
                    m_pos = p;
                    maybeAttachQuantifier(out, std::make_unique<BackReferenceBlock>(digits, false));
                } else if (e == QChar('x')) {
                    if (m_pos + 3 >= m_in.size()
                        || !isHexDigit(m_in[m_pos + 2]) || !isHexDigit(m_in[m_pos + 3])) {
                        throw ParseException{QStringLiteral("\\x 转义需要 2 位十六进制"), m_pos};
                    }
                    const QString token = QStringLiteral("\\x%1%2")
                                              .arg(m_in[m_pos + 2], m_in[m_pos + 3]);
                    flushLiteral(/*keepLast=*/false);
                    m_pos += 4;
                    maybeAttachQuantifier(out, std::make_unique<CharClassBlock>(token));
                } else if (e == QChar('u')) {
                    if (m_pos + 2 < m_in.size() && m_in[m_pos + 2] == QChar('{')) {
                        int p = m_pos + 3; // 跳过 \u{
                        QString hex;
                        while (p < m_in.size() && m_in[p] != QChar('}')) {
                            if (!isHexDigit(m_in[p])) {
                                throw ParseException{
                                    QStringLiteral("\\u{...} 只能包含十六进制字符"), p};
                            }
                            hex.append(m_in[p]);
                            if (hex.size() > 6) {
                                throw ParseException{
                                    QStringLiteral("\\u{...} 最多支持 6 位十六进制"), p};
                            }
                            ++p;
                        }
                        if (p >= m_in.size()) {
                            throw ParseException{QStringLiteral("\\u{...} 缺少 '}'"), m_pos};
                        }
                        if (hex.isEmpty()) {
                            throw ParseException{
                                QStringLiteral("\\u{...} 需要至少 1 位十六进制"), m_pos};
                        }
                        ++p; // skip '}'
                        const QString token = QStringLiteral("\\u{%1}").arg(hex);
                        flushLiteral(/*keepLast=*/false);
                        m_pos = p;
                        maybeAttachQuantifier(out, std::make_unique<CharClassBlock>(token));
                    } else {
                        if (m_pos + 5 >= m_in.size()
                            || !isHexDigit(m_in[m_pos + 2]) || !isHexDigit(m_in[m_pos + 3])
                            || !isHexDigit(m_in[m_pos + 4]) || !isHexDigit(m_in[m_pos + 5])) {
                            throw ParseException{QStringLiteral("\\u 转义需要 4 位十六进制"), m_pos};
                        }
                        const QString token = QStringLiteral("\\u%1%2%3%4")
                                                  .arg(m_in[m_pos + 2], m_in[m_pos + 3],
                                                       m_in[m_pos + 4], m_in[m_pos + 5]);
                        flushLiteral(/*keepLast=*/false);
                        m_pos += 6;
                        maybeAttachQuantifier(out, std::make_unique<CharClassBlock>(token));
                    }
                } else if (e == QChar('A') || e == QChar('z') || e == QChar('Z')) {
                    throw ParseException{
                        QStringLiteral("不支持的边界转义: \\%1").arg(e), m_pos};
                } else {
                    // 其它转义 (如 \+ \. \\) 作为"转义字符原子"保留.
                    flushLiteral(/*keepLast=*/false);
                    m_pos += 2;
                    auto blk = std::make_unique<CharClassBlock>(QStringLiteral("\\%1").arg(e));
                    maybeAttachQuantifier(out, std::move(blk));
                }
            } else if (c == QChar('.')) {
                flushLiteral(/*keepLast=*/false);
                auto blk = std::make_unique<CharClassBlock>(QStringLiteral("."));
                ++m_pos;
                maybeAttachQuantifier(out, std::move(blk));
            } else if (c == QChar('^') || c == QChar('$')) {
                flushLiteral(/*keepLast=*/false);
                out.push_back(std::make_unique<AnchorBlock>(QString(c)));
                ++m_pos;
            } else if (c == QChar('[')) {
                flushLiteral(/*keepLast=*/false);
                auto blk = parseCharSet();
                maybeAttachQuantifier(out, std::move(blk));
            } else if (c == QChar('(')) {
                flushLiteral(/*keepLast=*/false);
                auto blk = parseGroup();
                maybeAttachQuantifier(out, std::move(blk));
            } else if (c == QChar('?') || c == QChar('*') || c == QChar('+')
                       || c == QChar('{')) {
                throw ParseException{
                    QStringLiteral("量词 '%1' 不能放在最前面").arg(c), m_pos};
            } else {
                if (isQuantifierAt(m_pos + 1)) {
                    flushLiteral(/*keepLast=*/false);
                    auto blk = std::make_unique<LiteralBlock>(QString(c));
                    ++m_pos;
                    maybeAttachQuantifier(out, std::move(blk));
                } else {
                    litBuffer.append(c);
                    ++m_pos;
                }
            }
        }
        flushLiteral(/*keepLast=*/false);
        return out;
    }

    void maybeAttachQuantifier(std::vector<std::unique_ptr<Block>> &out,
                               std::unique_ptr<Block> atom) {
        out.push_back(std::move(atom));
        if (m_pos >= m_in.size()) return;
        const QChar c = m_in[m_pos];
        if (c == QChar('?') || c == QChar('*') || c == QChar('+') || c == QChar('{')) {
            auto q = parseQuantifier();
            if (q) out.push_back(std::move(q));
        }
    }

    std::unique_ptr<Block> parseQuantifier() {
        const int start = m_pos;
        const QChar c = m_in[m_pos];
        int mn = 0, mx = -1;
        if (c == QChar('?')) { mn = 0; mx = 1; ++m_pos; }
        else if (c == QChar('*')) { mn = 0; mx = -1; ++m_pos; }
        else if (c == QChar('+')) { mn = 1; mx = -1; ++m_pos; }
        else if (c == QChar('{')) {
            ++m_pos;
            QString numA, numB;
            while (m_pos < m_in.size() && m_in[m_pos].isDigit()) {
                numA.append(m_in[m_pos]); ++m_pos;
            }
            if (numA.isEmpty()) {
                throw ParseException{QStringLiteral("'{' 后需要数字"), start};
            }
            mn = numA.toInt();
            mx = mn;
            if (m_pos < m_in.size() && m_in[m_pos] == QChar(',')) {
                ++m_pos;
                while (m_pos < m_in.size() && m_in[m_pos].isDigit()) {
                    numB.append(m_in[m_pos]); ++m_pos;
                }
                mx = numB.isEmpty() ? -1 : numB.toInt();
            }
            if (m_pos >= m_in.size() || m_in[m_pos] != QChar('}')) {
                throw ParseException{QStringLiteral("缺少 '}'"), start};
            }
            ++m_pos;
        } else {
            return nullptr;
        }
        bool lazy = false;
        if (m_pos < m_in.size() && m_in[m_pos] == QChar('?')) {
            lazy = true; ++m_pos;
        } else if (m_pos < m_in.size() && m_in[m_pos] == QChar('+')) {
            throw ParseException{QStringLiteral("不支持的特性: 占有量词"), m_pos};
        }
        return std::make_unique<QuantifierBlock>(mn, mx, lazy);
    }

    std::unique_ptr<Block> parseCharSet() {
        const int start = m_pos;
        ++m_pos;   // 跳过 '['
        if (m_pos < m_in.size() && m_in[m_pos] == QChar('[')
            && m_pos + 1 < m_in.size() && m_in[m_pos + 1] == QChar(':')) {
            throw ParseException{QStringLiteral("不支持的特性: POSIX 字符类"), m_pos};
        }
        bool negated = false;
        if (m_pos < m_in.size() && m_in[m_pos] == QChar('^')) {
            negated = true; ++m_pos;
        }
        QString chars;
        while (m_pos < m_in.size() && m_in[m_pos] != QChar(']')) {
            const QChar ch = m_in[m_pos];
            if (ch == QChar('\\')) {
                if (m_pos + 1 >= m_in.size()) {
                    throw ParseException{QStringLiteral("末尾悬挂 '\\' 在字符集中"), m_pos};
                }
                const QChar e = m_in[m_pos + 1];
                if (e == QChar('x')) {
                    if (m_pos + 3 >= m_in.size()
                        || !isHexDigit(m_in[m_pos + 2]) || !isHexDigit(m_in[m_pos + 3])) {
                        throw ParseException{QStringLiteral("\\x 转义需要 2 位十六进制"), m_pos};
                    }
                    chars.append(QStringLiteral("\\x%1%2").arg(m_in[m_pos + 2], m_in[m_pos + 3]));
                    m_pos += 4;
                } else if (e == QChar('u')) {
                    if (m_pos + 2 < m_in.size() && m_in[m_pos + 2] == QChar('{')) {
                        int p = m_pos + 3; // 跳过 \u{
                        QString hex;
                        while (p < m_in.size() && m_in[p] != QChar('}')) {
                            if (!isHexDigit(m_in[p])) {
                                throw ParseException{
                                    QStringLiteral("\\u{...} 只能包含十六进制字符"), p};
                            }
                            hex.append(m_in[p]);
                            if (hex.size() > 6) {
                                throw ParseException{
                                    QStringLiteral("\\u{...} 最多支持 6 位十六进制"), p};
                            }
                            ++p;
                        }
                        if (p >= m_in.size()) {
                            throw ParseException{QStringLiteral("\\u{...} 缺少 '}'"), m_pos};
                        }
                        if (hex.isEmpty()) {
                            throw ParseException{
                                QStringLiteral("\\u{...} 需要至少 1 位十六进制"), m_pos};
                        }
                        ++p; // skip '}'
                        chars.append(QStringLiteral("\\u{%1}").arg(hex));
                        m_pos = p;
                    } else {
                        if (m_pos + 5 >= m_in.size()
                            || !isHexDigit(m_in[m_pos + 2]) || !isHexDigit(m_in[m_pos + 3])
                            || !isHexDigit(m_in[m_pos + 4]) || !isHexDigit(m_in[m_pos + 5])) {
                            throw ParseException{QStringLiteral("\\u 转义需要 4 位十六进制"), m_pos};
                        }
                        chars.append(QStringLiteral("\\u%1%2%3%4")
                                         .arg(m_in[m_pos + 2], m_in[m_pos + 3],
                                              m_in[m_pos + 4], m_in[m_pos + 5]));
                        m_pos += 6;
                    }
                } else if (e == QChar('c')) {
                    if (m_pos + 2 >= m_in.size() || !isAsciiLetter(m_in[m_pos + 2])) {
                        throw ParseException{QStringLiteral("\\c 转义需要一个字母, 如 \\cI"), m_pos};
                    }
                    chars.append(QStringLiteral("\\c%1").arg(m_in[m_pos + 2]));
                    m_pos += 3;
                } else if (e == QChar('p') || e == QChar('P')) {
                    int p = m_pos + 2;
                    if (p >= m_in.size() || m_in[p] != QChar('{')) {
                        throw ParseException{
                            QStringLiteral("Unicode 属性需要写成 \\%1{...}").arg(e), m_pos};
                    }
                    ++p; // skip '{'
                    QString prop;
                    while (p < m_in.size() && m_in[p] != QChar('}')) {
                        prop.append(m_in[p]);
                        ++p;
                    }
                    if (p >= m_in.size() || prop.isEmpty()) {
                        throw ParseException{QStringLiteral("Unicode 属性缺少 '}'"), m_pos};
                    }
                    ++p; // skip '}'
                    chars.append(QStringLiteral("\\%1{%2}").arg(e, prop));
                    m_pos = p;
                } else {
                    chars.append(QChar('\\'));
                    chars.append(e);
                    m_pos += 2;
                }
            } else {
                chars.append(ch);
                ++m_pos;
            }
        }
        if (m_pos >= m_in.size()) {
            throw ParseException{QStringLiteral("缺少 ']'"), start};
        }
        ++m_pos;   // 跳过 ']'
        return std::make_unique<CharSetBlock>(chars, negated);
    }

    std::unique_ptr<Block> parseGroup() {
        const int start = m_pos;
        ++m_pos;   // 跳过 '('
        bool capturing = true;
        QString groupName;
        if (m_pos < m_in.size() && m_in[m_pos] == QChar('?')) {
            ++m_pos;
            if (m_pos >= m_in.size()) {
                throw ParseException{QStringLiteral("'(?' 后缺少内容"), start};
            }
            const QChar t = m_in[m_pos];
            if (t == QChar(':')) {
                capturing = false; ++m_pos;
            } else if (t == QChar('=') || t == QChar('!')) {
                throw ParseException{QStringLiteral("不支持的特性: 环视 (?%1...)").arg(t), start};
            } else if (t == QChar('<')) {
                if (m_pos + 1 < m_in.size()
                    && (m_in[m_pos + 1] == QChar('=') || m_in[m_pos + 1] == QChar('!'))) {
                    throw ParseException{QStringLiteral("不支持的特性: 环视 (?<%1...)")
                                         .arg(m_in[m_pos + 1]), start};
                }
                ++m_pos; // skip '<'
                while (m_pos < m_in.size() && m_in[m_pos] != QChar('>')) {
                    groupName.append(m_in[m_pos]);
                    ++m_pos;
                }
                if (m_pos >= m_in.size() || groupName.isEmpty()) {
                    throw ParseException{QStringLiteral("命名分组缺少 '>'"), start};
                }
                ++m_pos;
                capturing = true;
            } else if (t == QChar('P')) {
                if (m_pos + 1 >= m_in.size() || m_in[m_pos + 1] != QChar('<')) {
                    throw ParseException{QStringLiteral("不支持的 (?P 语法"), start};
                }
                m_pos += 2;
                while (m_pos < m_in.size() && m_in[m_pos] != QChar('>')) {
                    groupName.append(m_in[m_pos]);
                    ++m_pos;
                }
                if (m_pos >= m_in.size() || groupName.isEmpty()) {
                    throw ParseException{QStringLiteral("命名分组缺少 '>'"), start};
                }
                ++m_pos;
                capturing = true;
            } else {
                throw ParseException{
                    QStringLiteral("不支持的分组语法 (?%1...)").arg(t), start};
            }
        }
        auto inner = parseAlternation(/*topLevel=*/false);
        if (m_pos >= m_in.size() || m_in[m_pos] != QChar(')')) {
            throw ParseException{QStringLiteral("缺少 ')'"), start};
        }
        ++m_pos;

        auto group = std::make_unique<GroupBlock>(capturing, groupName);
        for (auto &b : inner) group->children().push_back(std::move(b));
        return group;
    }

    static void appendAll(std::vector<std::unique_ptr<Block>> &dst,
                          std::vector<std::unique_ptr<Block>> &&src) {
        for (auto &b : src) dst.push_back(std::move(b));
    }

    const QString &m_in;
    int m_pos = 0;
};

} // namespace

RegexParser::Result RegexParser::parse(const QString &regex) {
    Result r;
    if (regex.isEmpty()) {
        r.ok = true;
        return r;
    }
    Parser p(regex);
    try {
        r.blocks = p.parseTop();
        r.ok = true;
    } catch (const ParseException &e) {
        r.ok = false;
        r.error = e.msg;
        r.errorPos = e.pos;
    }
    return r;
}
