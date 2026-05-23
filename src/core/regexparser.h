#pragma once

#include <QString>
#include <memory>
#include <vector>

class Block;

// 反向解析器: 把正则字符串解析回 Block 树.
// 用于右上角输入框直接输入正则 → 自动布局到画布.
//
// 支持子集 (覆盖现有 22 积木 + GroupBlock + 7 内置模板):
//   - 字面量, \d \D \w \W \s \S .
//   - 量词 ? * + {n} {n,m} {n,}, 可加 ? 表示 lazy
//   - 字符集 [abc] [a-z] [^...]
//   - 锚点 ^ $ \b \B
//   - 或 |
//   - 分组 (...) (?:...) (?<name>...) (?P<name>...)
//   - 反向引用 \1 / \k<name>
//   - Unicode 属性 \p{...} / \P{...}
//   - 常见转义 \xFF \u0041 \u{1F600} \000 \0 \cI \t \n \v \f \r \+
//
// 不支持 (返回 ok=false + 错误位置):
//   - 环视 (?= ?! ?<= ?<!)
//   - POSIX 类 [[:alpha:]]
//   - 占有量词 ++, *+
//   - 其它复杂方言语法 (如条件分支等)
class RegexParser {
public:
    struct Result {
        bool ok = false;
        std::vector<std::unique_ptr<Block>> blocks;
        QString error;
        int     errorPos = -1;
    };

    static Result parse(const QString &regex);
};
