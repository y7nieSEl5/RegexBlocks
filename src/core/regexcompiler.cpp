#include "core/regexcompiler.h"

#include "core/block.h"

#include <functional>

namespace {

// 递归编译一个 Block 序列, 累积 warning 与 valid 状态.
QString compileSequence(const QList<Block*> &blocks, RegexCompiler::Result &r) {
    if (blocks.isEmpty()) return {};

    QString out;
    out.reserve(blocks.size() * 4);

    Block *prev = nullptr;
    for (int i = 0; i < blocks.size(); ++i) {
        Block *b = blocks[i];
        if (!b) continue;

        // 量词必须紧跟可被修饰的项: 字面量 / 字符类 / 字符集 / GroupBlock 都可
        if (b->type() == BlockType::Quantifier) {
            if (!prev) {
                r.valid = false;
                if (r.warning.isEmpty())
                    r.warning = QStringLiteral("量词不能放在最前面: 它需要修饰一个前置项");
            } else if (prev->type() == BlockType::Quantifier
                       || prev->type() == BlockType::Anchor
                       || prev->type() == BlockType::Alternation) {
                r.valid = false;
                if (r.warning.isEmpty())
                    r.warning = QStringLiteral("量词不能修饰量词/锚点/或");
            }
        }

        if (b->type() == BlockType::Group) {
            // 递归: 先把子序列编译出来, 再外加括号
            auto *g = static_cast<GroupBlock*>(b);
            QList<Block*> kids;
            kids.reserve(static_cast<int>(g->children().size()));
            for (const auto &c : g->children()) kids.append(c.get());
            const QString inner = compileSequence(kids, r);
            if (!g->capturing()) {
                out += QStringLiteral("(?:%1)").arg(inner);
            } else if (!g->name().isEmpty()) {
                out += QStringLiteral("(?<%1>%2)").arg(g->name(), inner);
            } else {
                out += QStringLiteral("(%1)").arg(inner);
            }
        } else {
            out += b->toRegex();
        }
        prev = b;
    }
    return out;
}

} // namespace

RegexCompiler::Result RegexCompiler::compile(const QList<Block*> &blocks) {
    Result r{QString(), true, QString()};
    r.regex = compileSequence(blocks, r);
    return r;
}
