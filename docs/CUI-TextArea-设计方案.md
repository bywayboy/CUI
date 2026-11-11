# CUI TextArea 设计方案

> 状态：已实施
> 关联：`src/ui/ui_textarea.*`、`samples/textarea.c`、白皮书 §8.17

## 1. 目标

提供一个表单级多行纯文本编辑器：

- 软换行、显式换行、垂直滚动（复用 `UiScrollView`）；
- 鼠标定位/拖选、Shift 扩展、双击/三击不做（本批）；
- 键盘编辑、Home/End、Ctrl+Home/End、方向键、Ctrl+左右按词移动；
- Ctrl+A/C/X/V，多行剪贴板；
- 中文输入法：组合串就地绘制、上屏、候选窗跟随插入点；
- placeholder、readonly、disabled；
- 主题化外观、焦点/光标闪烁。

非目标：富文本、撤销栈、语法高亮、Tab 缩进、自动换行策略配置、双向文本。

## 2. 文本存储与 IME

`UiEdit` 现在也是可增长缓冲（`maxLen<=0` 表示不限长），但它是单行编辑，
`Edit_Char/InsertString` 会过滤换行。TextArea 因此使用自己的可变长 UTF-16 缓冲：

```c
wchar_t *text;   /* HeapAlloc/HeapReAlloc 按需倍增 */
int cap, len, caret, anchor;
```

不再有 4096 字符 / 2048 行的硬上限；`UiTextArea_Init(ta,id,0)` 即不限长。
TextArea 退出时调用 `UiTextArea_Shutdown` 释放文本、行索引和 UTF-8 缓冲。

IME 通过一个 **UiEdit 代理** 复用现有 `UiIme`：

```c
UiEdit imeEdit;   /* 只接收上屏文本和组合串; 同样动态增长 */
```

- 聚焦时 `UiIme_SetFocus(&imeEdit)`；
- `UiIme_Comp` 把 GCS_RESULTSTR 插入 `imeEdit.text`，组合串写入 `imeEdit.comp`；
- 每次 `UiTextArea_OnIme` / Draw / Key / Char 先 `ta_drain_ime()`：
  把 `imeEdit.text` 插入 TextArea 当前光标，然后清空代理；
- 组合串直接从 `imeEdit.comp` 取出，画在 TextArea 光标处并加下划线；
- 候选窗定位：绘制时把 TextArea 光标坐标写入
  `imeEdit.caretDipX/textTopDip/textHDip/hasRect`，`UiIme_Pos` 无需改动。

这样不改 `UiIme`/`UiEdit` 的 512 字符存储，也不把 TextArea 变成 UiEdit 的特例。

## 3. 行布局

```c
int *lineStart, *lineLen;   /* 动态行索引 */
float *lineW;               /* 每行宽度 (末尾追加续排用) */
int lineCap, lineCount;
float lineH;                /* Gfx_TextHeight(fInput) + 4 */
float wrapW;                /* text viewport width */
```

布局有三种模式，避免每敲一个字符就重排全文：

- **FULL**：初始化、`SetText`、宽度变化、插入/删除换行 → 全文重排；
- **APPEND**：光标在文档末尾追加普通字符 → 只从最后一行继续量新字符，
  成本 O(新增字符)；
- **REFLOW**：段落内普通编辑 → 只从编辑所在行重排到该段落结尾，
  后续段落只平移 `lineStart`（O(lineStart 数组) 的 memmove/加法）。

每行记录宽度，APPEND 不需要重测旧内容。行索引、UTF-8 转换缓冲都按需增长。

算法（逐字符累加宽度，O(n)）：

1. 从行起点开始，逐字符前进（代理对按 2 个 UTF-16 单元处理）；
2. 累加宽度，超过 wrapW 且当前行非空时断行；
3. 遇到 `\n` 结束当前行，下一行从 `\n+1` 开始；
4. 末尾 `\n` 产生一个空行；空文本视为 1 行。

宽度测量用 TextArea 自带的动态 UTF-8 转换缓冲，避免大栈数组。
`Gfx_TextWidth` 必须使用 `DWRITE_TEXT_METRICS.widthIncludingTrailingWhitespace`：
默认 `width` 会裁掉行尾空格，逐字符累加时把空格当 0 宽，行宽/光标 x 会比
实际渲染短，导致行末词语选不到。

## 4. 光标 / 选区

- `caret` 是 UTF-16 下标；`anchor != caret` 表示选区。
- 行号：最后一个 `lineStart <= caret` 的行；字符位置等于下一行起点时归下一行。
- 列号 = `caret - lineStart`，clamp 到 `lineLen`（`\n` 不算列）。
- 鼠标：
  - 行 = `(mouseY - textTop + scrollY) / lineH`；
  - 列 = 该行内“字符前/字符后”两个最近候选中距离 mouseX 更小者
    （不能把跨字符后的候选直接写成行末）；
  - 点击设置 anchor=caret；Shift+点击保留 anchor；
  - 按下后 `dragging=1`，按住期间持续更新 caret 扩展选区；
  - 鼠标定位不触发 `ensureCaret` 自动滚动；否则拖选时 offset 被光标拉走，
    同一鼠标位置会映射到别的行/列，出现来回跳；
  - 拖选到视口上下边缘 6 DIP 内或外部时，`ta_drag_autoscroll` 每帧滚动
    6–40 DIP（越远越快），并自失效下一帧；滚动后用新 offset 重新映射 caret，
    选区随滚动扩展。
- 垂直移动保存 `xPref`（当前光标 x），目标行按最近 x 选列；水平移动清除。
- 光标/选区都按行绘制，选区跨行时每行画一段；行尾换行选中画 4 DIP 小段。

## 5. 滚动

复用 `UiScrollView`：

- `sv.id = ta->id`（与字段同一个 ID：字段矩形就是滚动视口，命中/滚轮共用）；
- padding `{12, 8, 12, 8}`；`reserveSpace=1`，滚动条不压文字；
- 滚动条宽 6、minThumb 24、track 透明、常显；
- `wheelStep=lineH*3`、`wheelTauMs=0`（立即滚动）；
- `Begin(rc, wrapW, lineCount*lineH)` → 画可见行 → `End` 画滚动条；
- 光标移动/输入后 `ensureCaret`，Begin 之后用
  `UiScrollView_ScrollTo(...,0)` 把光标行滚入视口；
- End 后 `UiScrollView_Tick(16)` 让轨道翻页写入的 target 立即生效。

## 6. 键位

| 键 | 行为 |
| --- | --- |
| 字符 / IME 上屏 | 插入；有选区先替换 |
| Enter | 插入 `\n` |
| Backspace / Delete | 删除选区或前/后一个字符（代理对整体删除） |
| Left / Right | 前/后一个字符；Shift 扩展 |
| Ctrl+Left / Right | 按词移动（字母/数字/下划线/CJK 为词） |
| Up / Down | 上一/下一视觉行，保持 xPref |
| Home / End | 行首/行尾；Shift 扩展 |
| Ctrl+Home / End | 文档首/尾 |
| PageUp / PageDown | 上/下翻一屏，保持 xPref |
| Ctrl+A | 全选 |
| Ctrl+C / X / V | 复制/剪切/粘贴（换行写 CRLF，粘贴归一化为 LF） |
| Tab | 不处理，交给应用做焦点切换 |
| Esc | 不处理，应用可清焦点 |

组合期间 Enter/Esc/Tab/方向键交给输入法（与 UiInput 一致）。
Readonly：可选择/复制，不插入/删除/粘贴；Disabled：不注册命中、不滚动、不显示光标。

## 7. 绘制顺序

1. `Ui_DrawSurface` 画字段（inputBg/border/focus/glow）；
2. Disabled：手动 clip，画文本，结束；
3. 计算 `wrapW = UiScrollView_ContentWidth(sv, rc)`，重建行；
4. `UiScrollView_Begin`；
5. 选区高亮（每行一段）；
6. 文本行；IME 组合所在行分段绘制：前缀 → 组合串 + 下划线 → 后缀右移组合宽度；
7. placeholder（空文本）；
8. 光标（聚焦、闪烁、非 readonly）；
9. `UiScrollView_End` + `Tick`。

## 8. 焦点

- 点击字段 `Ui_SetFocusId(id)`；TextArea 每帧用 `ui->focusId == id` 判断。
- 焦点从无到有：`UiIme_SetFocus(&imeEdit)`、重置闪烁。
- 焦点丢失：`UiIme_SetFocus(NULL)`，取消组合。
- `clickedOutside` 且当前焦点是自己时清 focusId。
- 光标闪烁由应用 530ms timer 调 `UiTextArea_Blink`。

## 9. 边界

- 文本/行索引无固定上限；`maxLen<=0` 不限长，正数仍可限制表单长度。
- 插入/删除换行、改变控件宽度会触发全文重排；普通字符编辑只重排编辑行到
  段落末尾。单一超长段落中间编辑的成本仍与该段剩余长度线性相关。
- 不做双向文本/字素簇换行；emoji 代理对按整体处理。
- 不做撤销栈、Tab 插入、双击选词。

## 10. 验证计划

- 初始多行文本、placeholder、readonly/disabled 外观；
- 输入/Enter/Backspace 换行与行数变化；
- 长行软换行、滚动条出现、滚轮/thumb/轨道翻页；
- 鼠标点击定位、拖选、Shift+方向键选区；
- Ctrl+A/C/X/V 多行往返；
- IME：组合串显示、上屏进入正文、候选窗跟随光标；
- 7000+ 字符追加（超过旧 4096 上限）、7000+ 字符中段插入后
  Ctrl+A/C 回读与原文逐字一致、中段删除/回退正常；
- `UiTextArea_Shutdown` / `UiInput_Shutdown` / `Edit_Shutdown` 释放；
- 100%/200% DPI、深浅主题、静止 CPU 0。
