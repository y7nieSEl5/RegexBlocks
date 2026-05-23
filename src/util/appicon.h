#pragma once

// 跨平台应用图标生成 - 用 QPainter 运行时绘制, 无需外部图片文件
// 在三平台都立即可用: macOS Dock / Windows 任务栏 / Linux 任务栏

#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QFont>
#include <QLinearGradient>
#include <QPainterPath>

namespace ui {

inline QPixmap makeIconPixmap(int size) {
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    // 圆角方形背景 (蓝色渐变, 与 QSS 主题一致)
    const qreal r = size * 0.22;
    QLinearGradient grad(0, 0, 0, size);
    grad.setColorAt(0.0, QColor("#42A5F5"));
    grad.setColorAt(1.0, QColor("#1565C0"));
    p.setBrush(grad);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(0, 0, size, size), r, r);

    // 大写字母 R (代表 Regex)
    QFont f;
    f.setPointSize(static_cast<int>(size * 0.55));
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter,
               QStringLiteral("R"));

    // 右下角点缀: 一个橙色小方块代表 "积木"
    const qreal bs = size * 0.18;
    p.setBrush(QColor("#FFA726"));
    p.drawRoundedRect(QRectF(size - bs - r * 0.3, size - bs - r * 0.3,
                             bs, bs),
                      bs * 0.25, bs * 0.25);
    return pix;
}

inline QIcon makeAppIcon() {
    QIcon icon;
    for (int sz : {16, 24, 32, 48, 64, 128, 256, 512}) {
        icon.addPixmap(makeIconPixmap(sz));
    }
    return icon;
}

} // namespace ui
