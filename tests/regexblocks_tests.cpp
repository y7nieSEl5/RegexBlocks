// 往返一致性测试: compile(parse(s)) == s, 覆盖所有内置模板 + 含 GroupBlock 的复杂正则.
//
// 这是一个最小化测试入口, 不依赖 QtTest:
//   - 直接用 main() + 简单断言
//   - 失败时打印 diff, 退出码非零让 CI 立即可见
//
// 构建: cmake 时配置 -DREGEXBLOCKS_BUILD_TESTS=ON, 然后 ctest 或直接运行 regexblocks_tests.

#include "core/block.h"
#include "core/regexcompiler.h"
#include "core/regexparser.h"
#include "core/templates.h"

#include <QCoreApplication>
#include <QStringList>
#include <QString>
#include <QtGlobal>

#include <cstdio>
#include <vector>

namespace {

int g_failures = 0;

void check(bool cond, const QString &label) {
    if (cond) {
        std::printf("  [ OK ] %s\n", qPrintable(label));
    } else {
        ++g_failures;
        std::printf("  [FAIL] %s\n", qPrintable(label));
    }
}

QList<Block*> rawList(const std::vector<std::unique_ptr<Block>> &v) {
    QList<Block*> out;
    out.reserve(static_cast<int>(v.size()));
    for (const auto &b : v) out.append(b.get());
    return out;
}

// 规范化等价但不同写法 (例 \d{1,1} 与 \d, 等). 简单替换即可.
QString normalize(const QString &s) {
    QString out = s;
    out.replace(QStringLiteral("{1,1}"), QStringLiteral("{1}"));
    return out;
}

void testRoundTrip(const QString &original, const QString &label) {
    auto parsed = RegexParser::parse(original);
    if (!parsed.ok) {
        ++g_failures;
        std::printf("  [FAIL] %s: 解析失败 - %s (位置 %d)\n",
                    qPrintable(label),
                    qPrintable(parsed.error),
                    parsed.errorPos);
        return;
    }
    const auto compiled = RegexCompiler::compile(rawList(parsed.blocks));
    const QString a = normalize(compiled.regex);
    const QString b = normalize(original);
    if (a != b) {
        ++g_failures;
        std::printf("  [FAIL] %s\n         原: %s\n         得: %s\n",
                    qPrintable(label), qPrintable(original), qPrintable(compiled.regex));
    } else {
        std::printf("  [ OK ] %s   '%s'\n", qPrintable(label), qPrintable(original));
    }
}

void testParseFails(const QString &input, const QString &whyShouldFail) {
    auto r = RegexParser::parse(input);
    if (r.ok) {
        ++g_failures;
        std::printf("  [FAIL] 不支持特性应被拒绝: %s (输入 '%s')\n",
                    qPrintable(whyShouldFail), qPrintable(input));
    } else {
        std::printf("  [ OK ] 正确拒绝 %s\n", qPrintable(whyShouldFail));
    }
}

} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    std::printf("=== RegexBlocks Round-Trip Tests ===\n\n");

    // --- 1. 基础单积木 ---
    std::printf("[1] 基础积木\n");
    testRoundTrip(QStringLiteral("\\d"),    "字符类 \\d");
    testRoundTrip(QStringLiteral("\\w"),    "字符类 \\w");
    testRoundTrip(QStringLiteral("."),      "任意字符 .");
    testRoundTrip(QStringLiteral("a"),      "字面量 a");
    testRoundTrip(QStringLiteral("abc"),    "字面量 abc");
    testRoundTrip(QStringLiteral("\\."),    "转义字面量 \\.");
    testRoundTrip(QStringLiteral("^"),      "锚点 ^");
    testRoundTrip(QStringLiteral("\\b"),    "锚点 \\b");
    testRoundTrip(QStringLiteral("\\B"),    "锚点 \\B");
    testRoundTrip(QStringLiteral("[abc]"),  "字符集 [abc]");
    testRoundTrip(QStringLiteral("[a-z]"),  "字符集 [a-z]");
    testRoundTrip(QStringLiteral("[^0-9]"), "取反字符集 [^0-9]");
    testRoundTrip(QStringLiteral("[\\s\\S]"), "字符集 [\\s\\S]");
    testRoundTrip(QStringLiteral("\\p{L}+"), "Unicode 属性 \\p{L}+");
    testRoundTrip(QStringLiteral("\\u{FFFF}"), "Unicode 花括号转义 \\u{FFFF}");
    testRoundTrip(QStringLiteral("\\u{1F600}+"), "Unicode 花括号转义 + 量词");
    testRoundTrip(QStringLiteral("\\cI"), "控制字符转义 \\cI");
    testRoundTrip(QStringLiteral("\\t\\n\\v\\f\\r\\0"), "常见控制转义 \\t\\n\\v\\f\\r\\0");
    testRoundTrip(QStringLiteral("[\\u{4E2D}\\t]"), "字符集内花括号 Unicode + 控制转义");

    // --- 2. 量词 ---
    std::printf("\n[2] 量词\n");
    testRoundTrip(QStringLiteral("\\d?"),       "量词 ?");
    testRoundTrip(QStringLiteral("\\d*"),       "量词 *");
    testRoundTrip(QStringLiteral("\\d+"),       "量词 +");
    testRoundTrip(QStringLiteral("\\d{3}"),     "量词 {3}");
    testRoundTrip(QStringLiteral("\\d{1,5}"),   "量词 {1,5}");
    testRoundTrip(QStringLiteral("\\d{2,}"),    "量词 {2,}");
    testRoundTrip(QStringLiteral("\\w+?"),      "lazy 量词 +?");

    // --- 3. 或 ---
    std::printf("\n[3] 或\n");
    testRoundTrip(QStringLiteral("a|b"),         "顶层 a|b");
    testRoundTrip(QStringLiteral("\\d+|\\w+"),   "\\d+|\\w+");

    // --- 4. 分组 (GroupBlock) ---
    std::printf("\n[4] 分组\n");
    testRoundTrip(QStringLiteral("(?:abc)"),       "非捕获分组");
    testRoundTrip(QStringLiteral("(abc)"),         "捕获分组");
    testRoundTrip(QStringLiteral("(?:abc)+"),      "分组 + 量词");
    testRoundTrip(QStringLiteral("(\\d+|\\w+)"),   "分组内 alternation");
    testRoundTrip(QStringLiteral("(?:\\d{3}-\\d{4})"), "分组内序列");
    testRoundTrip(QStringLiteral("(?:(?:ab)+)"),   "嵌套分组");
    testRoundTrip(QStringLiteral("(?<word>\\w+)"), "命名捕获分组");
    testRoundTrip(QStringLiteral("(?<word>\\w+)\\k<word>"), "命名引用 \\k<word>");
    testRoundTrip(QStringLiteral("(\\d+)\\1"), "数字引用 \\1");
    testRoundTrip(QStringLiteral("\\xFF\\000"), "转义 \\xFF / \\000");

    // --- 5. 7 个内置模板 ---
    std::printf("\n[5] 内置模板往返\n");
    const auto templates = tpl::allTemplates();
    for (const auto &t : templates) {
        testRoundTrip(t.sampleRegex, t.name);
    }

    // --- 6. 复杂混合 ---
    std::printf("\n[6] 复杂正则\n");
    testRoundTrip(QStringLiteral("^https?://(?:www\\.)?[^/\\s]+"),
                  "URL with optional www");
    testRoundTrip(QStringLiteral("(?:abc){2,5}?"),
                  "嵌套分组 + 区间 lazy");

    // --- 7. 不支持特性应被拒绝 ---
    std::printf("\n[7] 不支持特性 (应被解析器拒绝)\n");
    testParseFails(QStringLiteral("(?=abc)"),    "前瞻 (?=...)");
    testParseFails(QStringLiteral("(?!abc)"),    "否定前瞻 (?!...)");
    testParseFails(QStringLiteral("(?<=abc)"),   "后顾 (?<=...)");
    testParseFails(QStringLiteral("[[:alpha:]]"), "POSIX 类");
    testParseFails(QStringLiteral("\\Aabc"),      "边界变体 \\A");
    testParseFails(QStringLiteral("\\u{XYZ}"),    "非法的 \\u{...} 十六进制");

    // --- 8. 不支持特性的字面量后备路径 (UI 处理) ---
    std::printf("\n[8] 解析失败时 errorPos 字段\n");
    {
        auto r = RegexParser::parse(QStringLiteral("(?=abc)"));
        check(!r.ok && r.errorPos >= 0,
              QStringLiteral("不支持语法错误带位置 (实际: %1)").arg(r.errorPos));
    }

    // --- 9. 量词拖动绑定属性序列化 ---
    std::printf("\n[9] 量词绑定属性\n");
    {
        auto q = std::make_unique<QuantifierBlock>(1, -1, false, false);
        const QJsonObject json = q->toJson();
        auto restored = Block::fromJson(json);
        auto *qr = dynamic_cast<QuantifierBlock*>(restored.get());
        check(qr && !qr->linkedToPrevForDrag(),
              QStringLiteral("量词 linkDrag 序列化/反序列化保持 false"));
    }

    std::printf("\n=== %s ===\n",
        g_failures == 0 ? "ALL TESTS PASSED" :
        qPrintable(QStringLiteral("FAILED: %1 test(s) failed").arg(g_failures)));
    return g_failures == 0 ? 0 : 1;
}
