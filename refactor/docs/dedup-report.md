# 重复实现审查与去重报告

2026-10-11。范围：`src/`（产品实现）与 `tests/`（验收测试）两部分；本次只处理产品实现，
测试侧重复单独统计并列为后续项。原工程文件未做任何修改，全部改动落在 `refactor/` 内。

> 1.2.0 变更说明：本报告记录的是 1.1.1 去重重构的审查结论。此后按用户要求整体移除了
> “团队共享库/工艺批注”，`refactor/src/collaborationdialog.*`、`refactor/src/apiclient.*`
> 及其测试已删除，因此下文第 2 节第 17 条、第 3 节末尾的文件清单与第 4 节第 2、3 条中
> 提到这些文件的部分只对 1.1.1 及标签 `v1.1.1` 成立，对当前版本不再适用。

## 1. 审查方法

1. 通读 `src/` 全部 25 个文件（约 4.9 千行有效代码）。
2. 用词法规范化克隆检测（`tools/find_clones.py`，标识符/数字/字符串归一化，30 词滑窗 +
   最长公共延伸，剔除法注释、字符串与预处理指令）统计重复片段：
   - 原始 `src/`：规范化词 49964，≥60 词重复组 60；≥150 词 5。
   - 去重后 `refactor/src/`：规范化词 49004，≥60 词重复组 50；≥150 词 5（残留见第 4 节）。
3. 对每个候选片段人工确认“是否真的重复实现”，再决定合并方式；只合并语义等价、
   错误文案可保持逐字一致的部分。

## 2. 已消除的重复实现

下表位置均指**原工程文件**（未改动），新位置指 `refactor/src/`。

| # | 原位置 | 重复内容 | 去重方式 / 新位置 |
| --- | --- | --- | --- |
| 1 | `sceneview.cpp:980-983`、`equipmentrepository.cpp:92-95` | 逐字相同的 `positive()`（可选数值有限且大于零） | `numericutil.h` 的 `NumericUtil::finitePositive` |
| 2 | `sceneview.cpp:70-77`、`equipmentwindow.cpp:51-58` | `ostringstream` + classic locale 的数值格式化各写一份 | `numericutil.h` 的 `extendedNumberText` / `millimeterText` / `numberText` |
| 3 | `equipmentrepository.cpp:353-370`、`sceneview.cpp:1085-1094` | 形状→尺寸字段规则与位置有限性判断各写一份 | `partvalidation.h` 的 `hasPositiveShapeDimensions` / `hasFinitePosition` / `usesOnlyShapeFields` |
| 4 | `equipmentrepository.cpp:376-381`、`sceneview.cpp:1100-1103`、`cadresultparser.cpp:312-315` | “网格拓扑 + 外包络一致性”两步校验重复三处 | `partvalidation.h` 的 `validateMeshPart` |
| 5 | `equipmentimporter.cpp:33-67`、`cadresultparser.cpp:207-226` | JSON 字段集合比较、非空白文本、有限数值/三元组判断各写一份 | `jsonutil.h` 的 `hasExactKeys` / `isNonBlankString` / `isFiniteNumber` / `readFiniteTriple` |
| 6 | `recordstore.cpp:199-230`、`equipmentrepository.cpp:113-125,136-169` | 连接名注册、忙等待、目录创建、打开、关闭与移除连接 | `sqliteconnection.h` 的 `SqliteConnection::open/close` |
| 7 | `equipmentrepository.cpp` 共 12 处（190/203/226/236/240/249/311/486/497/519/524/532…） | `error_ = QStringLiteral("…：%1").arg(query.lastError().text()); return false;` | 私有 `fail()` / `queryFailure()`（错误文案逐字不变） |
| 8 | `sceneview.cpp:313-335` 与 `338-353` | 相机球坐标换算 + 正/俯/右视图方向与上向量各写一份 | `cameraOrientation()`，供 `matrix()` 与 `cameraBasis()` 共用 |
| 9 | `sceneview.cpp:216-225`、`237-247`、`465-476`、`530-536` | NDC→画布像素映射四份、可绘制范围校验三份 | `canvasPoint()` / `finiteNdc()`；其中 `projectPoint()` 全工程无调用，作为死代码删除 |
| 10 | `sceneview.cpp:796-811`、`826-832`、`835-848`、`850-856`、`862-872`、`877-883` | 顶点+颜色交错填充三份、分栏视口/MVP/绘制循环三份 | `appendColoredVertex()` 与 `GlSurface::drawPanels()` |
| 11 | `sceneview.cpp:286-293`、`387-401`、`564-574`、`1363-1375` | 可见零件包围盒（含显示偏移）聚合四份 | `SceneData::visibleDisplayBox(low, high, only)` |
| 12 | `sceneview.cpp:148-159` 与 `262-266` | 1/2/5 档“整齐步长”算法两份 | `niceStep()` |
| 13 | `sceneview.cpp:298,580-582,634-636,671-673` | 轴名称数组与轴颜色映射分散四份 | `axisNames()` / `axisAccentColor()` / `axisGizmoColors()` |
| 14 | `sceneview.cpp:1055-1056,1304-1305,1471-1473` | 手势状态复位语句组三份 | `SceneView::Impl::resetGestures()` |
| 15 | `sceneview.cpp:694-695` 与 `820-821`（另见 712/834/840 的同类判定） | 线框（正交）模式判定两份、边绘制条件两份 | `lineFrameMode()`，两条绘制路径共用同一条件 |
| 16 | `equipmentcontroller.cpp:54-92` | JSON 与 CAD 两条“解析→保存→完整重新加载→如实报错”流程几乎逐行重复 | 私有 `commitImportedEquipment(equipment, parts, cadResult)`（错误文案逐字不变） |
| 17 | `collaborationdialog.cpp:263-267` 与 `309-313`；`276-284` 与 `335-342`；`535` 与 `30` | 状态标签样式两份；草稿范围保护两份；百分号编码内联一份 | `applyStatus()`、`scopeStillPresent()`、复用既有 `encoded()` |
| 18 | `cadimportprocess.cpp:316,361,382,488,518,524` 与 `317,489,513` | 停止两个定时器五处、退出预算启动三处 | `State::haltWatchdogs()` / `State::ensureExitBudget()` |

## 3. 行为保持的约束

- 所有公开类与公开成员函数签名、枚举、结构体字段、对象名（`objectName`）、
  用户可见错误文案与状态样式字符串保持逐字不变；`sceneview.h`、`main.cpp`、
  `meshgeometry.*`、`apiclient.*`、`mainwindow.*`、`cadimportprocess.h`、
  `cadresultparser.h`、`equipmentimporter.h`、`collaborationdialog.h`
  在本目录中与原文件逐字节一致（未被改动即无需复制修改）。
- 只合并语义等价的实现：例如仓库校验比场景校验多出的“网格来源字段”规则仍留在
  仓库侧（`meshSourceValid`），场景侧只复用尺寸/位置规则，不改变各自拒绝范围。
- 浮点与坐标约定未变：NDC→像素的 Y 轴翻转、三视图分栏比例、GL 视口原点换算、
  归一化 `long double` 保护、`float` 顶点预算均保持原公式。

## 4. 残留重复与未处理项（如实记录）

1. `equipmentwindow.h:59-88` 的成员声明序列、`equipmentwindow.cpp:161-190` 的
   `addValue(...)` 初始化序列会被滑窗算法判为“重复”：它们只是形态相同的声明/赋值行，
   不是重复实现，保留。
2. 小规模重复保留：`collaborationdialog.cpp` 的 `validNote()` 内多次
   `note.value(...)` 取值（同一对象重复取属性，提取后反而降低可读性）、
   `apiclient.cpp` 多条语义不同的“未导入资料”错误文案、三处状态标签样式的
   空白差异（`color: #b42318;` 与 `color:#b42318;`）——为保持已交付字符串未强行统一。
3. **测试侧重复未处理**：`tests/` 下检测到 241 组 ≥60 词重复片段，主要是
   `QTemporaryDir + EquipmentRepository + EquipmentController + EquipmentWindow`
   的窗口样板、`findChild` 控件定位块与跨文件夹具（例如
   `cad_user_tests.cpp:433` 与 `desktop_assembly_user_tests.cpp:325`、
   `cache_user_tests.cpp:111` 与 `shared_user_tests.cpp:122`）。本次不改测试，
   原因是测试是验收证据，提取公共 harness 需要单独功能卡、影响面复验与独立审查。
   建议后续按 `tests/support/harness.h`（窗口构造 + 控件定位 + 临时库 + 夹具）
   逐步迁移，一次只动一个测试目标并重跑该目标与其关联路径。

## 5. 验证证据

| 项目 | 命令 / 方式 | 结果 |
| --- | --- | --- |
| 配置 | `cmake --fresh -S refactor -B build-refactor -G Ninja`（Qt6.11.1 + GCC13.1.0 + Ninja） | 成功 |
| 编译 | `cmake --build build-refactor --clean-first --parallel 4`（`-Wall -Wextra -Wpedantic`） | 23 目标通过，0 警告 0 错误 |
| 自动检查 | `ctest --test-dir build-refactor --output-on-failure` | 18/18 通过（首次 31.09 秒、最终复验 27.78 秒） |
| 一键入口 | `.\refactor\build.ps1`（含 CAD 运行库同步到 `build-refactor/bin/cad-runtime`） | 全链跑通，CTest 18/18 通过 30.25 秒，未依赖环境变量 |
| 原生图形 | `build-refactor\bin\r11_desktop_tester.exe -platform windows --snapshot-dir build-refactor\r11-native` | `accepted=1`，`samples=4`，超采样 `1.5x`，退出码 0 |
| 测试未被削弱 | `.\refactor\tools\verify-harness-copy.ps1` | `checked=22 mismatches=0` |
| 重复度 | `tools/find_clones.py src` 与 `tools/find_clones.py refactor/src` | 60 组 → 50 组（≥60 词）；`sceneview.cpp` ≥80 词由 7 组 → 0 组 |
| 原工程未改 | `git status --short`；`git diff --stat -- src tests CMakeLists.txt scripts tools samples` | 仅新增 `refactor/`；原源码无差异 |
| 索引一致性 | `git rev-parse :refactor/tests/<f>` 与 `git rev-parse :tests/<f>` 逐文件比对 | 21 个测试文件与探针的 blob 完全相同，换行归一化后仍与原文件一致 |

未做/未验证：未在其它机器或其它 Qt 版本验证；未做长期资源与性能对比；未接真人验收；
未把本目录并入主树（合并需按补丁方式落到 `src/` 并重跑独立审查与真人验收）。
