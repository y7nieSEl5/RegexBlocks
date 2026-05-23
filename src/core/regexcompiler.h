#pragma once

#include <QString>
#include <QList>

class Block;

// 把 Block 序列编译成完整的正则字符串.
// 阶段 4: 线性拼接 (量词作为后置修饰符天然紧跟在前一项后).
// 高分版可在此扩展嵌套 GroupBlock 的递归处理.
class RegexCompiler {
public:
    struct Result {
        QString regex;       // 生成的正则字符串
        bool    valid;       // 当前 Block 序列是否合法
        QString warning;     // 警告 (如: 量词在最前面)
    };

    static Result compile(const QList<Block*> &blocks);
};
