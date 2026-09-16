# 第一轮检查（2026-09-16）

> **状态：9 条全部已修，2026-09-16 复查通过。**
> 复查方式：`python tools/build_all_tests.py` —— 12 个测试目标（比修之前多了
> `documenttab_smoke` 和 `shellintegration_smoke`）全部构建并通过；逐条核对了改法。
> 下面的原文保留，作为这一轮的记录。

对照 [PLAN.md](PLAN.md) 检查 M0/M1 的实现。

**先说结论**：骨架、分层、注释质量都对得上方案，`python tools/build_all_tests.py`
在本机跑通 —— 10 个测试目标全部构建并通过。下面按严重程度列问题。

---

## 1. 【严重】缩放和旋转之后，页面永远是灰色占位，再也不会显示

`src/view/pageview.cpp:95-108`（setZoom）和 `src/view/pageview.cpp:160-171`（setRotation）
里，**请求发在信号之前**：

```cpp
dropPageImages();
updateScrollBars();
requestVisiblePages();        // 这里发出 pageImageNeeded
Q_EMIT zoomChanged(clamped);  // DocumentTab 在这里才 invalidateRenders()
```

`DocumentTab` 是在 `zoomChanged` / `rotationChanged` 的槽里调 `invalidateRenders()`
的（`documenttab.cpp:37-48`）。于是顺序变成：

1. `requestVisiblePages()` 用**旧的** generation G 发出请求，`PageRenderer` 排队；
2. 紧接着 `zoomChanged` 触发 `invalidateRenders()`，generation 变成 G+1；
3. 渲染结果回来时 `isCurrent(G+1)` 为假 → 全部走 `discarded`，一张都不上屏；
4. `PageView::requested` 里已经记下了新的像素尺寸，`requestVisiblePages()` 的
   "on its way" 判断会一直跳过这些页 → **没有任何路径会再请求一次**。

结果：Ctrl+滚轮缩放、工具栏缩放、Ctrl+R 旋转之后，可见页全变成灰底加页码，
只有再触发一次 `dropPageImages()`（又一次缩放/旋转/换屏 DPI）才会重来一遍 —— 而那一次
同样会被丢掉。**等于缩放和旋转从第一次用就是坏的。**

打开文档那条路没事，因为 `open()` 里 `renderer->setDocument()` 先把 generation 顶掉了；
`resizeEvent` 也没事，因为 `applyFitIfNeeded()` 在 `requestVisiblePages()` 之前就发了
`zoomChanged`。这也是为什么程序开起来看着一切正常。

**改**：让"作废"发生在"请求"之前。最小改动是把这三处的 `Q_EMIT` 挪到
`requestVisiblePages()` 前面（`setZoom`、`setRotation`，`applyFitIfNeeded` 已经是对的）。
更稳的做法是 PageView 在任何作废时先发一个单独的 `invalidated()` 信号，
`requestVisiblePages()` 只在其后调用 —— 这样以后新增作废点不会再踩一次。

**另外补一个回归测试**：现在 `render_smoke` 只单测 `PageRenderer`，
`zoom_smoke` 只单测算术，中间的接线没人管，所以这个 bug 一个测试都没碰到。
需要一个把 `PageView` + `DocumentTab` 接起来的 smoke：打开夹具 → 等一张图上屏 →
改缩放 → 断言**最终有图上屏**（而不是断言请求发出去了）。

## 2. 【严重】用工具栏或输入框改缩放，模式还停在"适应宽度"

`PageView::setZoom()`（`pageview.cpp:95`）只改 `layout` 的 zoom，**从不碰
`zoomController`**。`ZoomController::setZoom()` 在整个产品代码里一次都没被调用过
（只有 `zoom_smoke` 调），所以 `fixedZoom` 永远是 1.0，头文件里"手动设的倍数能穿过
fit 模式活下来"那段承诺是不成立的。

两个能立刻看到的后果：

- 输入 200% 之后再 Ctrl+滚轮一格：`wheelEvent` 里 `basis = fittingZoom()`，而
  `fittingZoom()` 在非 Percent 模式下返回的是**适应宽度算出来的倍数**，于是直接
  跳回 fit×1.25，用户设的 200% 没了。
- 输入 200% 之后拖动窗口大小：`applyFitIfNeeded()` 看到模式还是 FitWidth，
  把 200% 改回适应宽度。

**改**：`PageView::setZoom()` 里调 `zoomController.setZoom(clamped)`
（它本身就会把模式切成 Percent），并发 `zoomModeChanged`。改完之后
`wheelEvent` 结尾那句 `setZoomMode(Percent)`（`pageview.cpp:401`）会变成空操作，
留着无妨。顺带 `ZoomController::effectiveZoom()` 目前也只有测试在用，
修好之后看看是让 PageView 走它、还是删掉 —— 两条路都行，但不要留着一个只有测试
证明其正确性的函数。

## 3. 【中】滚动条策略和它上面的注释正好相反

`pageview.cpp:44-47` 的注释写着"两个滚动条常驻，因为出现/消失会改变视口宽度，
进而改变适应宽度的含义，导致整篇重渲染 —— 这个闪烁是看得见的"，
下面两行却是 `Qt::ScrollBarAsNeeded`。注释描述的问题是真的（竖直滚动条一出现，
视口变窄 → 适应宽度倍数变小 → 全部重渲染）。

**改**：要么按注释改成 `ScrollBarAlwaysOn`，要么保留 `AsNeeded` 并把注释删掉。
不能留着一段说着反话的注释 —— 下一个人会以为这里已经处理过了。

## 4. 【中】`pagerenderer.h` 对线程模型的说明是错的

注释说 "QPdfPageRenderer in MultiThreaded mode keeps its own per-thread copies of
the document internally"。Qt 不是这么做的。查
`QtPdf/6.11.2/QtPdf/private/qpdfdocument_p.h`：Qt PDF 用的是一把**全局递归锁**
（`QPdfMutexLocker : std::unique_lock<QRecursiveMutex>`）把所有 FPDF 调用串起来，
document 没有任何副本。

这不是安全问题（锁在，所以 UI 线程调 `pageSize()` / `text()` / `metadata()` 不会崩），
但有两个后果要写对：

- **MultiThreaded 并不并行**。同一时刻只有一个 PDFium 调用在跑，它买到的是
  "不在 UI 线程上跑"，不是吞吐量。别指望多核加速。
- **UI 线程会被渲染阻塞**。M2 要在 UI 线程上调 `text()` / `textRuns()` / `links()`，
  一个 300ms 的扫描页渲染正在进行时，这些调用会排在那把全局锁后面 —— 界面就是卡住。
  所以 M2 的文本层和搜索必须自己挪到工作线程，或者做成异步。这一条现在就该写进
  `document.h` 的注释里，免得 M2 按"反正有锁"的想法直接调。

## 5. 【中·防御性】`removeKeyTree` 对空子键不设防

`src/windows/shellintegration.cpp:142-151` 调 `RegDeleteTreeW(hive, subKey.c_str())`，
而 `splitRoot()`（同文件 68-90）对 `"HKEY_CURRENT_USER\\"` 这种只有前缀的路径会返回
**true 且 subKey 为空**。`RegDeleteTreeW` 的空子键含义是"删掉这个 hive 下面的全部内容"。

今天**不可达** —— 两个调用点拼的都是编译期常量 `kProgId` / `kCascadeKey`，
`defaultClassesRoot()` 也是常量。但这正是 XYZip 那次事故的形状：
无条件递归删除 + 一个理论上可能为空的路径参数。守卫的代价是一行。

**改**：`splitRoot()` 在 subKey 为空时返回 false，或者 `removeKeyTree()` 开头
`if (subKey.empty()) return false;`。再补一个 smoke 断言
`removeKeyTree("HKEY_CURRENT_USER\\")` 返回 false。

## 6. 【小】以下几处是死代码或重复代码

- `pageview.cpp:200-210`，`requestVisiblePages()` 里两个 `if` 的条件后半段一样，
  第一个被第二个完全覆盖，`images.contains(page)` 那一路永远走不到。留一个。
- `RenderCache::dropExceptGeneration()` 只有 `cache_smoke` 在用。它本来是给"关文档"
  准备的，但 `DocumentTab::open()` 走的是 `cache.clear()`。要么让 open 用它，
  要么删掉 —— 现在的状态是"有一个只被自己的测试证明是对的函数"。
- `ZoomController::effectiveZoom()` 同上，见第 2 条。

## 7. 【小】`goToPage()` 会发两次 `currentPageChanged`，而且可能互相矛盾

`pageview.cpp:178-199`：先 `layout.setCurrentPage(clamped)`，再
`verticalScrollBar()->setValue()` —— 后者触发 `scrollContentsBy()`，那里用
`pageAtViewportCenter()` 重新算一次当前页并发信号；回到 `goToPage()` 又发一次
`clamped`。跳到一个比视口矮的页时，中点会落在下一页上，于是两个信号说的是两个页码。
状态栏和页码框会闪一下再回来。

**改**：跳转期间设个标志挡掉 `scrollContentsBy()` 里的那次发射，或者让 `goToPage()`
只设滚动位置、把发信号的事情完全交给 `scrollContentsBy()`。

## 8. 【小】`paintEvent` 里的 1:1 贴图在分数坐标上会掉像素行

`pageview.cpp:299` 关了 `SmoothPixmapTransform`，注释说图和目标矩形是 1:1 所以不需要
平滑。前提是目标矩形落在整数设备像素上，而页面是水平居中的，`rectOf()` 给出的
`left()` 经常是 .5。此时最近邻会重复或丢掉一行/一列像素，细线和小字看得出来。

**改**：给 image 设 `setDevicePixelRatio(ratio)` 然后用
`drawImage(QPointF, image)`，让 Qt 按 DPR 对齐；或者把目标矩形对齐到整数设备像素。

## 9. 【小】`QtPdfBackend::links()` 声明成 const 却改共享状态

`qtpdfbackend.cpp` 里 `links(page)` 调 `linksModel->setPage(page)`。model 是整个后端
共享的一份，翻页顺序一变结果就串。M2 要用它之前先想清楚：是每次查询建一个临时 model，
还是干脆别把这个函数标 const。
