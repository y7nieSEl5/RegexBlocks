#pragma once

#include <QString>
#include <QList>
#include <vector>
#include <memory>
#include <functional>

class Block;

namespace tpl {

struct Template {
    QString name;            // 菜单显示名
    QString description;     // tooltip
    QString sampleRegex;     // 预期生成的正则 (用于显示)
    std::function<std::vector<std::unique_ptr<Block>>()> build;
};

// 返回所有内置模板 (顺序对应 Ctrl+1 .. Ctrl+N 快捷键)
QList<Template> allTemplates();

} // namespace tpl
