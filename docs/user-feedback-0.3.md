# 0.3 模拟用户反馈与体验验收

日期：2026-10-08。角色：模拟工艺工程师 Agent。来源为模拟操作与自动化控件输入，不是真实客户反馈，不代替真实人工验收，也不承担代码审查。

## 情境与证据范围

工艺工程师先按名称检索零件，再在仍然可见的三维模型上点选目标；查看属性后习惯按 Escape 退出当前选择。本轮仅将这两个操作障碍列为0.3需求，其他想法保留候选，不自动扩大实施范围。

测试文件为 `tests/simulated_user_tests.cpp`，使用临时 SQLite 数据库与单个居中长方体的虚构设备。选择通过 `QTest::mouseClick` 输入真实控件，Escape 通过 `QTest::keyClick` 输入，不直接调用场景拾取回调或控制器选择函数冒充用户操作。

当前执行状态：两条0.3模拟需求已通过控件操作验收。主管实际执行0.2软件基线与0.3原生Windows验收；模拟用户 Agent 根据回传日志及独立查看截图记录结果。主管另报告10项CTest全部退出0、用时8.44秒，其中包含offscreen的 `simulated_user_acceptance`。这是模拟体验验收，不是真实人工验收；独立代码审查仍进行中。

## SU-03-01：搜索过滤后，点选可见模型需要额外清搜索

- 模拟反馈：我搜索后看到目标仍在模型里，直接点它想看材料和尺寸；界面应完成选择并让清单中选中项可见。
- 已确认表现：实际点击被过滤模型后没有选择，搜索保持 `not-found`、清单保持0项，状态提示要求先清空搜索。
- 已接受需求：点击有效且可见、但被搜索过滤的零件时，清空搜索并选中该零件；联动清单、属性及高亮，保留已有显隐状态。点击空白不清搜索；隐藏零件仍不可拾取。
- 操作：先点击居中模型验证目标能被鼠标拾取；用“清除选择”复位；在搜索框键入 `not-found`，确认清单0项；再次点击同一模型中心。
- 验收：搜索为空、清单选中 `SU-TARGET`、属性 ID 为同一值、控制器选择同一 ID，显隐状态不变；不能留下“已选中但清单无法定位”的状态。
- 基线运行证据：主管执行 `build/bin/simulated_user_tests.exe -platform offscreen --observe --snapshot-dir build/simulated-user-0.2`，退出0，渲染为软件投影；日志为 `SEARCH_FILTERED_CLICK accepted=0 selected='' search='not-found' rows=0 status='该零件被搜索条件过滤，请先清空搜索。'`。本角色已视觉检查 `build/simulated-user-0.2/02-filter-after-pick.png`：模型仍可见，清单为空，属性未显示。
- 决定与实施：主管已修复，本角色未改业务文件。模拟验收通过。
- 修复验收证据：主管实际执行 `build/bin/simulated_user_tests.exe -platform windows --snapshot-dir build/simulated-user-0.3-native`，退出0，`RENDERER` 为“原生OpenGL”；`SEARCH_FILTERED_CLICK accepted=1 selected='SU-TARGET' search='' rows=1`。本角色独立查看前后截图：搜索从 `not-found` 清空、目标回到清单并选中，模型由蓝色变为橙色高亮，属性显示同一ID、Q235及100/80/60毫米尺寸。
- 验收范围：已验证过滤后的居中可见模型点击及其清单/属性/高亮联动；本测试单目标起始显隐集合为空。点击空白保留搜索、已有其他隐藏零件不恢复等扩展边界由主管关联回归及独立审查覆盖，本角色不将未逐项视觉操作的情境记为全部人工验证。

## SU-03-02：Escape 清除当前选择

- 模拟反馈：查看完属性后希望按 Escape 清掉选择，继续看完整模型，不必去找右侧按钮。
- 已确认表现：四种焦点下实际按 Escape 后均保留 `SU-TARGET` 选择，搜索保持 `target`，隐藏数为1；没有完成清除选择。
- 已接受需求：窗口内清单、模型画布、搜索框和按钮获得焦点时，Escape 都执行清除选择；不关闭窗口，不清搜索，不恢复隐藏模型。
- 操作：搜索 `target`，鼠标选中清单零件，再点击“隐藏选中”；分别在清单、模型画布、搜索框及“显示全部”按钮焦点下按 Escape。每轮通过已有按钮恢复到相同状态。
- 验收：清单无选择且当前行 -1；属性 ID 清空、详细属性隐藏、“请选择零件”提示可见；隐藏及隔离按钮禁用；搜索仍为 `target`，目标仍隐藏。
- 基线运行证据：上述主管执行的同次基线中，`ESCAPE focus=list/canvas/search/button` 四条日志均为 `accepted=0 selected='SU-TARGET' search='target' hidden=1`。末行 `SIMULATED_USER observed=1 filteredPick=0 escape=0 mode=observe`，说明观察前提完成且两项体验均未满足。本角色已视觉检查 `build/simulated-user-0.2/03-escape-search.png`：清单选中和属性面板仍显示，模型仍隐藏。
- 决定与实施：主管已修复为窗口范围Escape清除选择。本角色未改业务文件。模拟验收通过。
- 修复验收证据：上述原生Windows同次运行中，`ESCAPE focus=list/canvas/search/button` 四条日志均为 `accepted=1 selected='' search='target' hidden=1`，末行 `SIMULATED_USER observed=1 filteredPick=1 escape=1 mode=accept`。本角色独立查看四种焦点截图：目标清单无选中高亮，属性改为“请选择零件”，隐藏与隔离按钮禁用，搜索仍是 `target`，目标仍带“已隐藏”且模型区域为空，窗口未关闭。

## 执行入口及结果解释

主管已统一接入目标 `simulated_user_tests`，链接 `equipment_window` 和 `Qt6::Test`。实际完成的直接运行入口为：

```powershell
build/bin/simulated_user_tests.exe -platform offscreen --observe --snapshot-dir build/simulated-user-0.2
build/bin/simulated_user_tests.exe -platform windows --snapshot-dir build/simulated-user-0.3-native
```

`--observe` 仅验证操作前提并输出基线表现；退出0不能解释为0.3体验通过。默认模式要求两项新行为全部满足，失败退出1。软件平台结果只说明回退控件路径；原生平台须以 `RENDERER` 输出和截图核实，不将软件回退等同于原生OpenGL。

基线offscreen截图的文字显示为方框，无法据此评价中文可读性；控件文本值由日志断言取证。已查看的0.3原生Windows六张截图中文与英文均可读，清单、按钮、属性和提示没有发现文字截断或遮挡。本轮不将offscreen平台现象单独扩大为0.3功能需求。

本角色查看的截图目录为 `build/simulated-user-0.3-native`，包含过滤点选前后两张与四种焦点Escape四张。没有重新运行主管测试，也没有改动测试主体或业务代码；模拟验收依据为主管实际操作日志和本角色独立视觉核实。

## 候选与边界

尚未通过真实操作取得证据的快捷键说明、导入文件记忆目录、材料分类检索及复杂设备性能均不列为本轮缺陷。0.3修复后继续从实际操作发现后续问题，不据猜想生成任务。
