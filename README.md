# RegexBlocks · 拖拽式正则表达式测试器

> Qt 程序设计实习课程作业 · 跨平台 (macOS / Windows / Linux)

像搭积木一样可视化构造正则表达式，告别手写晦涩的符号。
左侧积木库 → 中间画布拼接 → 右侧实时匹配高亮。
支持暗色模式、撤销/重做、替换、最近文件、窗口状态记忆、**Scratch 风格分组容器**、**双向编辑**(正则字符串 ↔ 积木树) 等专业级 UX。

---

## 本次更新（2026-05-23，单独标注）

> 以下为 **5.23** 本轮集中修复与增强，便于课程文档/答辩单独引用。

### 1) 左侧积木库 UX 打磨
- **去掉选中态的蓝色矩形**：之前点击左侧某个积木项后，整行会铺一条蓝色背景（icon 右侧露出一条多余的蓝条）。改为 `QListWidget::item:selected { background: transparent; }`，并加 `outline: none` 顺手去掉了部分平台上的虚线焦点框；拖拽逻辑用的是 `currentItem()` 不依赖选中态，功能不受影响。
- **字符类按正/反义成对重排**：原顺序 `\d, \w, \s, ., \D, \W, \S, \p{L}, \P{L}` 把正向类与反向类拆散了；新顺序 `.`、`\d / \D`、`\w / \W`、`\s / \S`、`\p{L} / \P{L}`，符合"概念配对"的直觉。

### 2) 画布选中模型刷新（单选默认 / Cmd 多选）
- **痛点**：Qt 默认行为只在"点击未选中项"时清空其它选中；如果之前通过 `Cmd+click` 或橡皮筋框选攒了多个选中，后续普通单击其中一个块时不会清空——导致紧接着的拖动会把别的块一起带走。
- **修复**：在 `BlockGraphicsItem::mousePressEvent` 入口，当未按住 `Cmd/Ctrl` 时立即把其它选中项 `setSelected(false)`，再交给基类做"选中当前块"。macOS 上 `Qt::ControlModifier` 自动映射到 `Cmd`，无需平台特判。
- **效果**：
  - 选 A → 拖 B：只移动 B。
  - 选 A → 单击 B：只 B 选中。
  - `Cmd+click A` 再 `Cmd+click B`：两个都选中（多选语义保留）。
  - 多选状态 → 普通点击其中一个：其它清掉，只这一个保留。
  - 多选状态 → `Cmd+click+drag` 一个：整组同步移动（原有多选拖动语义保留）。

### 3) 量词放置约束 + 单击不再制造非法状态
- **痛点 A（堆叠量词）**：之前 UI 拖拽允许把第二个量词放在已有量词的字符后面（产出 `a+*` 这种序列），尽管编译器会标 invalid + 警告、反向解析器也会拒绝，但拖拽层不拦——三层语义不一致。
- **修复 A**：`BlockCanvas::snapQuantifierTarget` 中新增 `slotOccupied` 标记，循环走过 base 后面的现有量词时把它置 true；若为 true 直接 `return nullptr`，复用已有的红色拒绝反馈链路（`m_quantifierDropInvalid = true`，提示"量词需放在可重复项后"）。
- **痛点 B（单击触发隐式重排）**：在 `a+*` 状态下单击 `+`，块会被悄悄移到 `*` 后面变成 `a*+`。
- **根因**：`BlockCanvas::onItemDragEnded` 在 line 614 提前把 `m_draggedItem = nullptr;`，然后才调用 `dropTargetAt` / `snapQuantifierTarget`；这两个函数靠 `cand == m_draggedItem` 排除被拖块自身，`m_draggedItem` 已是 `nullptr` 时 walk-past-quantifier 循环会"走过自己"，把 `insertIdx` 推到列表末尾，导致出现一次伪 Move。
- **修复 B**：给 `dropTargetAt` 和 `snapQuantifierTarget` 加可选参数 `excludeItem = nullptr`（默认行为退化到 `m_draggedItem`，对 `computeDropIndicator` / `dropEvent` 等其它调用方零侵入）。`onItemDragEnded` 阶段显式把本地 `dragged` 传进去，自身排除恢复正确。
- **效果**：
  - 构造 `a+*`：从调色板再拖第二个量词到 `a` 附近 → 红色拒绝。
  - 已有 `a+*`：单击 `+` 或 `*`，物理顺序都不再变化。
  - 量词在不同 base 之间正常拖动 / 拖到无效位置回弹：行为不变。

### 4) Rust 代码片段切换到 `fancy-regex`
- **痛点**：之前"复制 ▾ → Rust"始终生成 `regex` crate (RE2 引擎) 的代码。但工具自 5.18 起已支持 `BackReferenceBlock`（`\1` / `\k<name>`），用户构造出反向引用后，生成的 Rust 代码会在 `regex::Regex::new(...).unwrap()` 处 panic——三层语义（UI / 编译 / 导出片段）再次不一致。
- **修复**：`snippetRust()` 改为输出 `fancy-regex = "0.18"` 版本，外加 `use fancy_regex::Regex;`；`find_iter` 在 fancy-regex 里返回 `Iterator<Item = Result<Match, Error>>`，loop 体里多一次 `let m = m.unwrap();` 解包。
- **顺手清理**：菜单标签 `Rust (regex crate, RE2)` → `Rust (fancy-regex)`，tooltip 重写为"在 regex 之上加回溯 VM，纯 NFA 子集会自动委派"。README 跨语言表格把 Rust 行从 `regex / RE2` 改成 `fancy-regex / 回溯 VM + 内嵌 regex NFA`；同时删掉"反向引用 \1 — Rust 不支持"这条已经过时的"刻意未支持"说明。
- **为什么不智能切换**：fancy-regex 对无 fancy 特性的简单正则会零成本委派回 `regex`，所以一直用它即可，避免多一条代码路径。

### 5) 替换语法文案修正 + 不支持特性显式标注
- **痛点**：右下角"替换为"输入框 placeholder 写的是 `支持 $1 $2 ... 反向引用`，但底层用的是 Qt `QString::replace(QRegularExpression, QString)`，它只认 `\0 \1..\99` 反斜杠语法——用户照 placeholder 输入 `$1` 会原样输出 `$1`，会被误判为 bug。
- **修复**：
  - placeholder 改为 `输入替换内容 (\0 = 整段匹配, \1 \2 = 捕获组), 留空可清除匹配`
  - "替换全部"按钮 tooltip 扩写：明确列出支持的 `\0 \1..\99`，并显式标注**不支持** JS 风格的 `$0 $1 $& $\` $'`、以及 `\n \t` 转义。
- **README 同步**：`### 替换模式` 小节扩成两张表（支持 / 不支持），方便答辩/课程文档直接引用；并解释设计取舍——"直接复用 Qt 原生替换、不加一层 JS 风格预处理"是为了避免 `$` 和 `\` 在同一个字符串里出现"双重解释"。
- **影响**：纯文案修正，零代码路径变更，不影响序列化、撤销栈、`replacement` JSON 字段。

### 6) 回归验证
- 重新构建通过：`./scripts/build-mac.sh`
- 测试通过：`build/regexblocks_tests`（`ALL TESTS PASSED`）

---

## 本次更新（2026-05-18，单独标注）

> 以下为 **5.18** 本轮集中修复与增强，便于课程文档/答辩单独引用。

### 1) 导出与可发现性
- 右上角 `复制 ▾` 新增 **C++ (Qt / QRegularExpression)** 代码片段导出。
- Flags 区标题改为更明显的 `Flags (i/g/m/u/y/s)`，降低“找不到选项”的成本。

### 2) 语法能力扩展（PCRE2 / Qt 优先）
- 新增命名捕获组与引用：`(?<name>...)`、`\k<name>`。
- 新增数字反向引用：`\1`、`\12`。
- 新增 Unicode 属性类：`\p{L}`、`\P{L}`。
- 新增常见转义支持：`\+`、`\000`、`\xFF`、`\u4E2D`。

### 3) 积木库增强
- 左侧积木库新增可直接拖拽项：`\B`、`[\s\S]`、命名分组、数字/命名引用、转义字符组。
- 不再需要“先拖一个锚点再去编辑”才能得到 `\B`。

### 4) 量词交互重做（第二轮）
- 拖动“被修饰项”时，紧随其后的量词会自动跟随移动并重新吸附。
- 画布常驻显示“被修饰项 -> 量词”的连线，绑定关系可视化。
- 拖动量词时会高亮吸附目标；若落点无效，显示红色提示（`量词需放在可重复项后`）。
- 支持临时解绑手势：**按住 Alt 再拖动**，只移动主块，不携带后续量词。

### 5) 量词交互增强（第三轮）
- 新增**自动吸附到最近合法目标**：拖动量词时，会自动贴靠到同一层级中最近的可修饰项后方。
- 量词右键菜单新增显式开关：`拖动时跟随前一项`（支持锁定/解锁量词随动行为）。
- 对于已解锁量词，积木右上角显示 `free` 标识，便于区分是否参与主块联动拖拽。

### 6) 回归验证
- 重新构建通过：`./scripts/build-mac.sh`
- 测试通过：`build/regexblocks_tests`（`ALL TESTS PASSED`）

---

## 正则方言 (Dialect Compatibility)

**底层引擎**：Qt `QRegularExpression`，**PCRE2 兼容**（Perl-Compatible Regular Expressions, 与 PHP / Nginx 同款引擎）。

**跨语言可移植性**：当前 22 个内置积木 + GroupBlock 涉及的特性（字符类、量词、字符集、锚点、分支、捕获/非捕获分组），在以下方言上语义完全一致，生成的正则可以**原样**复制到对应语言中使用：

| 语言 | 库 | 引擎 | 一键导出 |
|---|---|---|---|
| Python | `re` | PCRE-like | 复制 ▾ → Python |
| JavaScript | `RegExp` | Irregexp | 复制 ▾ → JavaScript |
| Java | `java.util.regex.Pattern` | PCRE-like | 复制 ▾ → Java |
| Rust | `fancy-regex` | 回溯 VM + 内嵌 `regex` NFA，支持反向引用 / 环视 | 复制 ▾ → Rust |
| C++ Qt | `QRegularExpression` | PCRE2 | 复制 ▾ → PCRE / 原样 |

> Rust 一栏选用 `fancy-regex` 而非更轻量的 `regex` crate，是因为本工具自 5.18 起已支持反向引用 `\1` / `\k<name>` 积木，纯 RE2 引擎的 `regex` crate 会直接拒绝这类模式。`fancy-regex` 对没有 fancy 特性的简单正则会自动委派回底层 `regex` 引擎，性能基本无损。

**刻意未支持的方言敏感特性**（确保跨语言通用）：

- 环视 `(?=...) (?!...) (?<=...) (?<!...)` — 当前积木集未提供（如未来加入，Rust 端 `fancy-regex` 也已覆盖）
- 命名分组语法差异：PCRE/Java `(?<name>)` vs Python `(?P<name>)` — 反向解析时**统一接受**两种语法，输出时统一为普通分组
- 占有量词 `++ *+ {..}+` — Java 独有
- POSIX 字符类 `[[:alpha:]]` — JS / Rust 不支持
- 边界变体 `\A \z \Z` — JS 不支持

> **结论**：当前积木集生成的正则在 5 门语言中完全通用，无需任何方言转换。
> 帮助菜单 → "正则方言说明" 也能查看本节内容。

---

## 功能特性

### 积木系统
- 22 个预制积木（字符类 / 量词 / 字符集 / 锚点 / 字面量 / 或）
- **Scratch 风格 GroupBlock 容器**（捕获 `(...)` / 非捕获 `(?:...)`）：子积木**物理嵌入**到容器内，可任意嵌套，可任意拖入/拖出
- 双击编辑参数（量词 `{n,m}`、字符集 `[abc]`、字面文本、分组捕获模式等）
- 7 种字符类、5 种量词预设、4 种常用字符集
- 6 种类别色（Material Design 饱和色，浅色/暗色模式下都清晰）

### 双向编辑（Bidirectional）
- **正向**（积木 → 正则）：拖动积木期间右上角文本框 60Hz 实时刷新；释放后跑匹配（200ms 防抖）
- **反向**（正则 → 积木）：右上角文本框可直接输入正则字符串，按 Enter 或失焦 → 自动反向解析回积木树并布局到画布
  - 不支持的特性（反向引用、环视、POSIX 类等）会弹对话框，可选择"取消"或"作字面量整段导入"
  - 整个反向解析进入 undo 栈，一次 ⌘Z 可撤销
- **代码片段导出**：右上角"复制 ▾"菜单一键生成 PCRE / C++ Qt / Python / JavaScript / Java / Rust 代码片段（自动转换 i/g/m/u/y/s flags）

### Scratch 风格拖拽反馈
- 拖动期间显示**半透明蓝色鬼影**预览目标插入位置
- 命中到分组容器内部时，**容器边框高亮发亮**
- 释放鼠标后**100ms 缓动滑入**（OutCubic）到目标位置

### 操作交互
- **拖拽**：左侧 → 画布拖入；画布内拖动重排；蓝色插入指示线
- **删除**：选中 + Delete（macOS 是 ⌫ Backspace）；右键删除；**拖出画布到红色区域**自动删除（半透明 + 红虚线提示）
- **右键菜单**：编辑 / 复制 / 删除
- **撤销/重做**：⌘Z / Ctrl+Z 全部覆盖（添加、删除、重排、编辑、清空、加载模板）
- **画布默认从最左侧开始**，启动后立即看到积木（不需要手动滚动）

### 实时正则
- 顶部正则字符串实时显示 + 一键复制
- **6 个正则选项**：忽略大小写 (`i`) / 全局 (`g`) / 多行 (`m`) / Unicode 属性 (`u`) / sticky (`y`) / `.` 匹配换行 (`s`)
- 测试文本中所有匹配段落自动高亮 + 匹配列表显示分组捕获
- 匹配数量显示（面板标签 + 状态栏）
- 200ms 防抖，错误正则红框提示
- **点击匹配项 → 编辑器自动选中并滚动到对应位置**

### 替换模式
- 在 "替换为" 输入框输入替换字符串，点 "替换全部" 一键替换测试文本中所有匹配
- 替换走 `QTextCursor::beginEditBlock()`，**整个替换可一次 ⌘Z / Ctrl+Z 撤销**

**支持的反向引用语法**（Qt `QString::replace(QRegularExpression, QString)` 原生支持）：

| 写法 | 含义 |
|---|---|
| `\0` | 整段匹配（whole match） |
| `\1` `\2` … `\99` | 第 N 个捕获组（与正则里的 `(...)` 顺序一一对应） |

**刻意/暂未支持的替换特性**（用户从其它工具迁移时需要注意）：

| 不支持的写法 | 在其它工具里的含义 | 当前行为 | 替代方案 |
|---|---|---|---|
| `$&` | 整段匹配（JS / Perl / PHP） | 原样输出 `$&` 两个字符 | 改用 `\0` |
| `$1` `$2` … | 捕获组（JS / Perl / PHP） | 原样输出 `$1` 等字符 | 改用 `\1` `\2` |
| `` $` `` | 匹配之前的文本（JS） | 原样输出 | — |
| `$'` | 匹配之后的文本（JS） | 原样输出 | — |
| `$$` | 字面量 `$`（JS / Perl 转义） | 原样输出 `$$` | 在 Qt 替换里 `$` 本来就是字面量，直接写 `$` 即可 |
| `\n` `\t` `\\` 等 C 风格转义 | 控制符 / 字面量反斜杠 | 原样输出 backslash + 字母 | 把控制符直接粘进输入框（QLineEdit 单行场景下意义有限） |

> 设计取舍：直接使用 Qt 的原生 `QString::replace` 一行实现，零依赖、跨平台行为完全一致；不引入 JS 风格预处理层避免与 Qt 自身语义出现"双重解释"。如果将来真的需要 JS 风格的 `$1` `$&` `` $` ``，可以加一层 helper 把它们映射到 Qt 的 `\N` 形式。

### 7 个常用正则模板（一键加载）
- 邮箱、中国手机号、URL、IPv4、日期 ISO、16 进制颜色、整数
- ⌘1 … ⌘7 / Ctrl+1 … Ctrl+7

### 暗色模式
- 视图(&V) → 主题 → ◉浅色 / ○暗色，**瞬时切换无需重启**
- 配色参考 VSCode 暗色风格（炭灰背景 `#1e1e1e`、深蓝选中）
- 高亮颜色在暗色模式下降饱和度，不刺眼
- 错误背景由亮红 → 暗红，体感舒适
- 积木类别色保持饱和不变（Scratch / Blockly 一致设计语言）

### 配方持久化
- `.regexblocks` JSON 格式（v4，向后兼容 v1/v2/v3）
- 文件菜单 → 保存 / 打开 / **最近文件**（最多记住 5 个）
- 标题栏显示当前文件名 + **未保存修改时显示 `*`**
- 关闭/新建/打开/加载模板前自动弹"是否保存"确认（避免丢失进度）

### 窗口状态记忆
- 窗口大小、位置、分割条比例、最近文件、主题选择，全部用 `QSettings` 持久化
- **第二次打开应用**：所有 UI 状态完全恢复

---

## 快捷键 · 跨平台对照

| 操作 | macOS | Windows / Linux | Qt 实现 |
|---|---|---|---|
| 新建配方 | ⌘N | Ctrl+N | `QKeySequence::New` |
| 打开配方 | ⌘O | Ctrl+O | `QKeySequence::Open` |
| 保存配方 | ⌘S | Ctrl+S | `QKeySequence::Save` |
| 撤销 | ⌘Z | Ctrl+Z | `QKeySequence::Undo` |
| 重做 | ⌘⇧Z | Ctrl+Y | `QKeySequence::Redo` |
| 删除选中积木 | ⌫ (Backspace) | Delete 或 Backspace | 监听两个键 |
| 复制积木（右键菜单） | ⌘D | Ctrl+D | `QKeySequence("Ctrl+D")` |
| 加载模板 1-7 | ⌘1..⌘7 | Ctrl+1..7 | `QKeySequence("Ctrl+N")` |
| 退出 | ⌘Q (在 RegexBlocks 菜单) | Ctrl+Q (或 Alt+F4) | `QKeySequence::Quit` |
| 关于 | RegexBlocks 菜单 → 关于 | 帮助菜单 → 关于 | `setMenuRole(AboutRole)` |
| 帮助 | ⌘? | F1 | `QKeySequence::HelpContents` |
| 切换主题 | 视图 → 主题 → 浅色/暗色 | 同左 | `QActionGroup` (无快捷键) |

> **跨平台原则**：所有快捷键都用 Qt `QKeySequence::StandardKey` 或 `QKeySequence(tr("Ctrl+X"))`。
> Qt 在 macOS 上自动把 `Ctrl` 翻译成 `Cmd`，同一份源码在三平台显示符合各自习惯的快捷键。

---

## 技术栈

- **语言**: C++17
- **框架**: Qt 6 Widgets（无任何第三方依赖）
- **构建**: CMake (>= 3.16) + Ninja (推荐)
- **跨平台**: macOS / Windows / Linux 同一份源码

---

## 构建与运行

### macOS

#### 选项 A: Homebrew + Ninja（最快）

```bash
brew install qt cmake ninja                 # 一次性安装依赖
cd RegexBlocks
./scripts/build-mac.sh                       # 一键构建
open ./build/RegexBlocks.app                 # 启动
```

或一键打包成可独立分发的 .dmg：

```bash
./scripts/build-mac.sh --dmg                 # 内嵌 Qt 框架, 接收方无需装 Qt
```

或手动：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
open ./build/RegexBlocks.app
```

#### 选项 B: 官方 Qt 安装器

到 https://www.qt.io/download-open-source 下载 Qt 6 在线安装器，
安装后用 `cmake -DCMAKE_PREFIX_PATH=~/Qt/6.7.0/macos ...` 显式指定路径。

### Windows

#### 依赖
- **Qt 6**：到 https://www.qt.io/download-open-source 下载在线安装器，勾选 `Qt 6.x.x → MSVC 2022 64-bit` 或 `MinGW 64-bit`
- **构建工具**：Visual Studio 2022 Build Tools（含 MSVC）或 MinGW；CMake 3.16+
- 推荐安装 Ninja: `winget install Ninja-build.Ninja`

#### 构建（Developer Command Prompt for VS 2022 中执行）

```bat
cd RegexBlocks
scripts\build-windows.bat
```

或手动：

```bat
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH="C:\Qt\6.7.0\msvc2022_64"
cmake --build build
windeployqt --release build\RegexBlocks.exe
build\RegexBlocks.exe
```

> `windeployqt` 会把 Qt DLL 复制到 build 目录，让 .exe 可独立分发。

### Linux

#### Ubuntu / Debian

```bash
sudo apt install qt6-base-dev cmake ninja-build
cd RegexBlocks
./scripts/build-linux.sh
./build/RegexBlocks
```

#### Fedora / RHEL

```bash
sudo dnf install qt6-qtbase-devel cmake ninja-build
./scripts/build-linux.sh
```

#### Arch Linux

```bash
sudo pacman -S qt6-base cmake ninja
./scripts/build-linux.sh
```

#### 系统级安装（可选）

```bash
sudo cmake --install build --prefix /usr/local
# 安装后可在应用启动器中找到 RegexBlocks 图标
```

### 在 Qt Creator 中打开（三平台通用）

`File → Open File or Project →` 选择 `CMakeLists.txt`，
配置好对应 Kit (Qt 6) → Configure Project → 点绿色三角形运行。

---

## 给同学/老师分发（无需装 Qt 即可运行）

### macOS 用户：直接发 .dmg

```bash
./scripts/build-mac.sh --dmg
# 生成 build/RegexBlocks.dmg (~30-50 MB, 内嵌 Qt 框架)
```

接收方双击 .dmg → 拖入 "应用程序" → 双击运行。

> **首次打开如弹"无法验证开发者"**：右键 `RegexBlocks.app` → "打开" → 确认。
> 或在终端：`xattr -cr /Applications/RegexBlocks.app`
> （仅因为没有 Apple 付费签名证书，与软件本身无关）

### Windows 用户：发 build/ 文件夹

```bat
scripts\build-windows.bat
REM 把整个 build/ 文件夹打包发给对方
```

`windeployqt` 已经把 Qt DLL 复制好了，对方解压后双击 `RegexBlocks.exe` 即可。

### Linux 用户：建议直接发源码

Linux 发行版差异大，让对方按上面构建步骤本地编译最稳妥。
如果有 AppImage / Flatpak 打包需求，参考 [linuxdeployqt](https://github.com/probonopd/linuxdeployqt)。

---

## 故障排查

### `find_package(Qt6) failed`

CMake 找不到 Qt。手动指定路径：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/<compiler>
# macOS Homebrew: /opt/homebrew/opt/qtbase
# Windows MSVC:  C:/Qt/6.7.0/msvc2022_64
# Linux 系统包:  通常自动找到
```

### Linux `xcb` 错误

Qt 在 Linux 需要 X11 平台插件。装：
- Ubuntu/Debian: `sudo apt install libxcb-cursor0`
- Fedora: `sudo dnf install xcb-util-cursor`

### macOS `Library not loaded`

如果 .app 拷到没装 Qt 的电脑上启动失败，需要先用 `macdeployqt` 把 Qt 库打包进去：

```bash
/opt/homebrew/opt/qtbase/bin/macdeployqt build/RegexBlocks.app -dmg
```

`scripts/build-mac.sh --dmg` 已自动做这一步。

### macOS "已损坏，无法打开"

接收方 macOS 出现这个提示，是 Gatekeeper 在没有签名的应用上加了隔离属性。让接收方运行：

```bash
xattr -cr /path/to/RegexBlocks.app
```

---

## 项目结构

```
RegexBlocks/
├── CMakeLists.txt              # 三平台 Qt 自动检测 + 平台特定打包
├── main.cpp                    # 入口 + 主题应用 + 应用图标
├── README.md
├── src/
│   ├── ui/
│   │   ├── mainwindow.{h,cpp}      # 主窗口 + 5 个菜单 + 未保存追踪 + 最近文件
│   │   ├── blockpalette.{h,cpp}    # 左侧积木库 (拖拽源, 主题响应)
│   │   ├── blockcanvas.{h,cpp}     # 中间画布 (走 undo stack, 主题响应)
│   │   ├── testpanel.{h,cpp}       # 右侧测试面板 + flags + 替换模式 + 点击跳转
│   │   ├── blockeditor.{h,cpp}     # 双击参数对话框
│   │   └── theme.{h,cpp}           # 应用主题 (Light/Dark) + 全局色板
│   ├── core/
│   │   ├── block.{h,cpp}           # Block 抽象 + 6 子类 + JSON
│   │   ├── regexcompiler.{h,cpp}   # Block 序列 -> 正则字符串
│   │   ├── commands.{h,cpp}        # 5 个 QUndoCommand
│   │   └── templates.{h,cpp}       # 7 个常用正则模板
│   ├── graphics/
│   │   └── blockgraphicsitem.{h,cpp}   # QGraphicsObject 渲染 + 自实现拖拽
│   └── util/
│       ├── fonts.h                 # 跨平台字体抽象
│       └── appicon.h               # 运行时图标生成 (无外部依赖)
├── resources/                   # 图标 / 桌面入口
│   ├── README.md                # 资源说明
│   └── regexblocks.desktop      # Linux 桌面入口
└── scripts/                     # 三平台一键构建脚本
    ├── build-mac.sh             # 含 --dmg 选项
    ├── build-linux.sh
    └── build-windows.bat        # 含 windeployqt
```

---

## 跨平台技术细节（答辩参考）

| 维度 | 实现 |
|---|---|
| Qt 版本 | Qt 6.x（统一各平台 API） |
| 编译器 | macOS Apple Clang / Windows MSVC 或 MinGW / Linux GCC 或 Clang |
| 构建系统 | CMake 单一 CMakeLists.txt，三平台条件编译 |
| 快捷键 | `QKeySequence::StandardKey` 自动适配 Cmd/Ctrl |
| 菜单角色 | `QAction::setMenuRole(AboutRole/QuitRole/AboutQtRole)` 让 Mac 自动归位 |
| 字体 | `QApplication::font()` + `QFontDatabase::systemFont(FixedFont)`，三平台用各自系统字体 |
| 删除键 | 同时监听 `Key_Delete` + `Key_Backspace`（Mac 标准键盘只有 Backspace） |
| 右键菜单 | `contextMenuEvent` 自动响应三平台不同的右键触发方式 |
| 对话框按钮 | `QDialogButtonBox` 自动按平台调整 OK/Cancel 顺序 |
| 应用图标 | `QPainter` 运行时生成，无需外部图片文件 |
| 配置存储 | `QSettings` 自动写入各 OS 标准位置 |
| 文件路径 | 全部 `QFile` / `QFileInfo`，自动处理路径分隔符 |
| 暗色主题 | `Fusion` style + 自定义 `QPalette`，三平台行为一致 |
| 标题脏位 | `setWindowModified(true)` + `[*]` 占位符跨平台显示 |
| QSS | 仅样式品牌按钮 + 结构控件，不覆盖原生输入框 |

---

## 设计决策（答辩参考）

### 为什么用 `QGraphicsView` 而不是普通 `QWidget` 拼？
- 积木需要自由位置 + 旋转 + 透明度 + Z 轴排序，`QGraphicsScene` 原生支持
- 内置碰撞检测、变换矩阵、视图缩放（未来扩展友好）
- 与 Scratch / Blockly 等同类工具的实现路径一致

### 为什么 Theme 是单例 + 信号？
- 全局变量无法订阅变化（painter 代码会拿到旧颜色）
- `Q_OBJECT` 单例 + `themeChanged` 信号是 Qt 推荐的"全局可观察对象"模式
- 每个 widget 独立订阅，不需要 MainWindow 转发

### 为什么暗色用 `Fusion` style？
- macOS / Windows / Linux 的原生 style 对 `setPalette` 响应不一致（macOS 大量控件不响应）
- Fusion 是 Qt 自带的跨平台 style，三平台行为完全一致
- 这是工业标准做法（Qt Creator、Krita 都是这么干的）

### 为什么积木颜色不跟主题变？
- Scratch / Blockly / Snap! 的设计语言：积木颜色编码"语义类别"（数字/字符串/控制流），不参与主题
- Material 饱和色在浅灰底和炭灰底上对比都足够

### 为什么自己实现 `mouseMoveEvent` 而不是用 `ItemIsMovable`？
- Qt 默认的 `ItemIsMovable` 在某些情况下会用 item-local 坐标当 scene 坐标，导致首次拖动时积木"瞬间锚到 (0,0)"
- 自己用 "按下时鼠标 scene 位置" 作为参考点，绝对增量法计算位置，彻底避免跳变

### 为什么 `setAlignment` 不能解决初始滚动问题？
- Qt 文档：`setAlignment` 只在"整个场景能装入视口"时起效
- 我们场景宽 2400 远超视口，需要在 `showEvent` 用 `QTimer::singleShot(0, ...)` 推迟到下一次事件循环再设 scrollbar 值（此时 viewport 才有真实尺寸）

### 为什么撤销栈不直接驱动脏位？
- 测试文本、flags、替换字串的修改也算脏，但不进 undo 栈
- 自己维护 `m_dirty` 标志位，与 undo 栈解耦更清晰

---

## 已知限制

- **不支持嵌套分组 UI**：复杂表达式如 `(a(bc|de)+)?` 需要手写到字面量积木里
- **不支持替换预览**：替换是直接原地修改测试文本（可 ⌘Z 撤销，但没有"预览模式"）
- **配方文件不存窗口/主题状态**：那些是 UI 偏好，存在 `QSettings`，跨设备同步配方时不会带过来
- **macOS 未签名**：分发 .app 时接收方需要手动允许打开（见故障排查）
- **不支持正则历史记录**：每次只保存当前状态，没有"上一个/下一个"按钮浏览过去的正则

---

## 配方文件格式 (`.regexblocks`)

JSON v4（向后兼容 v1/v2/v3，自动迁移）：

```json
{
  "version": 4,
  "blocks": [
    {"type": "charClass",  "klass": "\\d"},
    {"type": "quantifier", "min": 3, "max": 3, "lazy": false},
    {"type": "literal",    "value": "-"},
    {"type": "charClass",  "klass": "\\d"},
    {"type": "quantifier", "min": 4, "max": 4, "lazy": false}
  ],
  "testText": "联系电话 138-2345 或 010-1111",
  "flags":       ["i"],
  "replacement": "***-****"
}
```

字段说明：

| 字段 | 引入版本 | 说明 |
|---|---|---|
| `version` | v1 | 文件格式版本 |
| `blocks` | v1 | 积木序列, 顺序即正则拼接顺序 |
| `testText` | v1 | 当前测试文本 |
| `flags` | v2 | 启用的正则选项 (`i`/`g`/`m`/`u`/`y`/`s`) |
| `replacement` | v3 | "替换为"输入框的内容 |

---

## License

仅供课程作业演示使用。
