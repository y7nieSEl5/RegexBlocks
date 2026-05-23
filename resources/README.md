# resources/

跨平台资源目录。

## 文件说明

| 文件 | 平台 | 说明 |
|------|------|------|
| `regexblocks.desktop` | Linux | 桌面入口 (安装后写入 `/usr/share/applications/`) |
| `app_icon.png` (可选) | Linux | 512x512 PNG, 安装后写入 `/usr/share/icons/.../apps/regexblocks.png` |
| `app_icon.icns` (可选) | macOS | macOS App Bundle 图标. 用 `iconutil -c icns iconset/` 生成 |
| `app_icon.ico` (可选) | Windows | Windows .exe 嵌入图标 |
| `app_icon.rc` (可选) | Windows | 资源清单, 内容: `IDI_ICON1 ICON "app_icon.ico"` |
| `resources.qrc` (可选) | 通用 | Qt 资源清单, 把图片嵌入二进制 |

## 当前状态

应用窗口图标 (Dock / 任务栏) 由 [src/util/appicon.h](../src/util/appicon.h) 在运行时通过
QPainter 生成, **无需任何外部图标文件即可工作**。

如果想让 macOS Finder / Windows 资源管理器看到精美图标 (而不是默认占位符), 可以:

### macOS - 生成 .icns

```bash
mkdir app_icon.iconset
for sz in 16 32 64 128 256 512; do
    sips -z $sz $sz source.png --out app_icon.iconset/icon_${sz}x${sz}.png
    sips -z $((sz*2)) $((sz*2)) source.png --out app_icon.iconset/icon_${sz}x${sz}@2x.png
done
iconutil -c icns app_icon.iconset
# CMakeLists 会自动检测并集成
```

### Windows - 生成 .ico

```bash
# 用 ImageMagick:
magick convert source.png -define icon:auto-resize=256,128,64,48,32,16 app_icon.ico
# 然后创建 app_icon.rc 内容:
echo 'IDI_ICON1 ICON "app_icon.ico"' > app_icon.rc
```
