#include "core/templates.h"
#include "core/block.h"

namespace tpl {

namespace {

// 简化构造工具函数
inline std::unique_ptr<Block> Lit(const QString &s)         { return std::make_unique<LiteralBlock>(s); }
inline std::unique_ptr<Block> Cls(const QString &k)         { return std::make_unique<CharClassBlock>(k); }
inline std::unique_ptr<Block> Q  (int min, int max)         { return std::make_unique<QuantifierBlock>(min, max); }
inline std::unique_ptr<Block> Set(const QString &c, bool n=false) { return std::make_unique<CharSetBlock>(c, n); }

// 邮箱: \w+@\w+\.\w+
std::vector<std::unique_ptr<Block>> buildEmail() {
    std::vector<std::unique_ptr<Block>> v;
    v.push_back(Cls("\\w"));   v.push_back(Q(1, -1));
    v.push_back(Lit("@"));
    v.push_back(Cls("\\w"));   v.push_back(Q(1, -1));
    v.push_back(Lit("."));
    v.push_back(Cls("\\w"));   v.push_back(Q(1, -1));
    return v;
}

// 中国手机号: 1\d{10}
std::vector<std::unique_ptr<Block>> buildChinaPhone() {
    std::vector<std::unique_ptr<Block>> v;
    v.push_back(Lit("1"));
    v.push_back(Cls("\\d"));   v.push_back(Q(10, 10));
    return v;
}

// URL: https?://\S+
std::vector<std::unique_ptr<Block>> buildURL() {
    std::vector<std::unique_ptr<Block>> v;
    v.push_back(Lit("http"));
    v.push_back(Lit("s"));     v.push_back(Q(0, 1));   // s?
    v.push_back(Lit("://"));
    v.push_back(Cls("\\S"));   v.push_back(Q(1, -1));
    return v;
}

// IPv4: \d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}
std::vector<std::unique_ptr<Block>> buildIPv4() {
    std::vector<std::unique_ptr<Block>> v;
    for (int i = 0; i < 4; ++i) {
        v.push_back(Cls("\\d"));  v.push_back(Q(1, 3));
        if (i < 3) v.push_back(Lit("."));
    }
    return v;
}

// 日期 ISO: \d{4}-\d{2}-\d{2}
std::vector<std::unique_ptr<Block>> buildDateISO() {
    std::vector<std::unique_ptr<Block>> v;
    v.push_back(Cls("\\d"));  v.push_back(Q(4, 4));
    v.push_back(Lit("-"));
    v.push_back(Cls("\\d"));  v.push_back(Q(2, 2));
    v.push_back(Lit("-"));
    v.push_back(Cls("\\d"));  v.push_back(Q(2, 2));
    return v;
}

// 16进制颜色: #[0-9a-fA-F]{6}
std::vector<std::unique_ptr<Block>> buildHexColor() {
    std::vector<std::unique_ptr<Block>> v;
    v.push_back(Lit("#"));
    v.push_back(Set("0-9a-fA-F"));
    v.push_back(Q(6, 6));
    return v;
}

// 整数 (带可选负号): -?\d+
std::vector<std::unique_ptr<Block>> buildInteger() {
    std::vector<std::unique_ptr<Block>> v;
    v.push_back(Lit("-"));    v.push_back(Q(0, 1));   // -?
    v.push_back(Cls("\\d"));  v.push_back(Q(1, -1));
    return v;
}

} // namespace

QList<Template> allTemplates() {
    return {
        {QObject::tr("邮箱"),
         QObject::tr("简化版: 字符@字符.字符"),
         QStringLiteral("\\w+@\\w+\\.\\w+"),
         buildEmail},

        {QObject::tr("中国手机号"),
         QObject::tr("以 1 开头的 11 位数字"),
         QStringLiteral("1\\d{10}"),
         buildChinaPhone},

        {QObject::tr("URL"),
         QObject::tr("HTTP/HTTPS 链接"),
         QStringLiteral("https?://\\S+"),
         buildURL},

        {QObject::tr("IPv4"),
         QObject::tr("IPv4 地址 (不严格校验范围)"),
         QStringLiteral("\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}"),
         buildIPv4},

        {QObject::tr("日期 ISO"),
         QObject::tr("ISO 8601 日期 yyyy-MM-dd"),
         QStringLiteral("\\d{4}-\\d{2}-\\d{2}"),
         buildDateISO},

        {QObject::tr("16 进制颜色"),
         QObject::tr("#RRGGBB 格式"),
         QStringLiteral("#[0-9a-fA-F]{6}"),
         buildHexColor},

        {QObject::tr("整数"),
         QObject::tr("可带负号的整数"),
         QStringLiteral("-?\\d+"),
         buildInteger},
    };
}

} // namespace tpl
