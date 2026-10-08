# 0.5工艺批注与修订冲突模拟验收

日期：2026-10-08。角色：模拟工艺工程师 Agent。此文记录模拟操作方案和后续实际证据，不是真实客户反馈，不代替人工验收或独立代码审查。

## 需求来源与当前状态

0.4实际共享操作已通过。已查看的界面能共享不可变设备几何，所选零件只展示材料、形状、尺寸；没有记录工艺意见的入口。基于这一能力缺口，0.5新增所选设备/零件批注和修订冲突保护；尚未观察到现有批注功能丢失意见，也不将新增需求写成已发生缺陷。

本角色已亲自串行完成offscreen与windows两轮真实控件/HTTP/MySQL验收，均退出0，九项主情境及附加身份/旧回调保护通过，并独立查看九张新增Windows截图。测试只清本轮空ev_test所有权下的 annotations→part→equipment→schema_version，两轮清理均成功。代码审查和真实人工验收由相应角色另行记录。

## 两用户操作方案

两个真实EquipmentWindow各有独立临时SQLite与Controller，使用既有共享服务连接。显示名“工艺甲”“工艺乙”仅用于批注作者显示，不代表不同登录账号。QTest通过控件输入、鼠标选择和点击提交；不调用网络回调或直接修改业务状态替代操作。

| 编号 | 操作 | 必须观察到的结果 | 状态 |
| --- | --- | --- | --- |
| SU-05-01 | 甲在设备 `共享/工艺 ` 的所选 `零件/支架 ` 新增中文、多行、含引号意见；乙刷新同scope | 同一UUID批注、原始文本、作者甲、revision1正确；其他同名零件 `..` 不出现该零件意见 | 两平台通过 |
| SU-05-02 | 切换同名零件和设备 `..`，两设备均具有同一零件ID；再切回原scope | 批注严格按equipmentId+partId定位，点/斜线/中文/尾空格身份不归一化，不按名称串联 | 两平台通过 |
| SU-05-03 | 两窗口同时读同一批注revision1并各输入不同草稿；甲保存，乙随后提交旧版本 | 甲revision2成功；乙明确409冲突，乙草稿保留，原作者/关联/创建时间不变；无自动覆盖或自动重发 | 两平台通过 |
| SU-05-04 | 乙冲突后刷新，查看甲最新保存内容与revision2，再明确点击提交乙草稿 | 刷新不清乙草稿；已保存内容与草稿可分别辨认；仅最后显式提交才保存revision3，updatedBy为乙 | 两平台通过 |
| SU-05-05 | 新增请求真正提交到MySQL后，受控代理丢弃一次HTTP响应；不改草稿，用户显式重试新增 | 同UUID原payload重试返回当前DTO；乙已修订时，甲仍保留不同草稿，随后显式PUT | 两平台通过 |
| SU-05-06 | 修改草稿后关闭本次服务，再刷新/提交 | 明确离线错误，草稿原文仍在，既有已保存内容/本地几何不被清掉；不自动提交 | 两平台通过 |
| SU-05-07 | 重建本次服务后乙刷新批注 | revision、作者、最后修改者、原文与精确scope持久存在；显示UTC时间并明确时区，不以系统本地时间冒充UTC | 两平台通过 |
| SU-05-08 | 选择设备级scope(partId=null)新增意见，再选择零件scope | 设备级意见关联null且不混为某个同名零件；按契约的设备全量列表/零件过滤规则展示 | 两平台通过 |
| SU-05-09 | 有草稿时按Escape/标准标题关闭，有pending时再次关闭；再点击“放弃草稿”关闭 | 草稿时保留对话框并只取消等待；显式放弃后才回收，父窗口选择和几何保持 | 两平台通过 |

未规定关闭对话框后永久保存草稿，冻结契约仅要求冲突、断网和刷新时保留；如果实现需要跨关闭持久草稿，应由主管明确需求，不据猜想扩大验收。

## 真实证据与故障控制计划

后端Agent先独占ev_test验证；本角色只有收到主管GUI就绪及后台空库可用通知后才运行。仍要求显式 `EV_TEST_CONFIG_FILE`、本机ev_test/3310/ev_app、空数据库；正常8765 API、ev_shared、MySQL80和正常SQLite不操作。

同UUID重试使用本机受控HTTP代理转发到真实API：等待POST成功响应已经完整到达代理后，故意断开一次到Qt的响应连接；保留请求UUID与payload用于测试核对，禁止打印令牌或完整业务正文。重试由用户再次点击按钮触发，代理正常转发；通过真实MySQL计数及UUID一致性确认没有重复。这是明示模拟错误场景，不声称真实网络曾发生同样故障。

代理已实现四项附加身份/生命周期操作：修改一次真实PUT响应的noteId与partId、修改一次零件过滤列表的partId、删除一次详情响应中的当前part、延迟一次已从服务取得的GET批注响应。Qt必须拒绝不匹配身份并保留草稿；dirty时拒绝切换scope；取消延迟请求后立即再次刷新，旧回调不能覆盖新状态。代理修改的是回包，不修改真实MySQL身份；PUT已实际保存时之后正常刷新会显示真实已提交内容，测试不承诺取消远端写入。

服务重建通过harness持有的createApp关闭/重新创建完成，保持同一测试库资料；只关闭本次ephemeral服务，不停止常规服务。UTC需以DTO的ISO时间、可见时间标签与存储epoch对应核对；截图不能单独证明时区正确或持久化。

最终将记录实际命令、退出码、Qt与harness日志、SQL结果、原生截图和独立查看结论。offscreen和windows串行；首次失败要区分测试预设错误、业务缺陷及环境故障，保存具体步骤和实际结果。尚未运行的用例不记为通过。

## 实测前观察与运行入口

读取GUI时发现更新时间以无时区的本地字符串显示；这是静态观察，还没有运行缺陷证据。主管已采纳并修正为明确UTC的ISO时间，测试将核DTO的Z结尾和可见UTC标签。

```powershell
$env:EV_TEST_CONFIG_FILE = 'D:/作业/简历/工作模拟/api/runtime/test.local.json'
node tests/run_shared_user.mjs build/bin/shared_user_tests.exe offscreen
node tests/run_shared_user.mjs build/bin/shared_user_tests.exe windows
```

后台Agent已通知0.5真实integration清理并释放ev_test，本角色在主管构建完成后独占运行，没有与其他ev_test测试并行。截图默认 `build/shared-user-0.5-offscreen` 与 `build/shared-user-0.5-windows`。原0.4发布/下载/错误/离线用例仍保留并通过。

## 首轮实际执行：设备选择待诊断

主管重编新目标后，本角色独占ev_test实际执行offscreen harness，退出1，约5.7秒。完整日志保存在 `build/shared-user-0.5-offscreen.log`。已输出发布/重复发布/modal Escape保护，以及另一客户端刷新/中文斜线尾空格ID下载/重复下载保护全部1；随后在 `chooseCombo` 点击弹出项选择设备 `..` 时，实际currentData未达到期望，触发断言。尚未执行0.5批注主情境，windows未运行。

harness输出 `cleanup=1 ownedTestTablesRemoved=1`，本轮拥有的四表已清理。当前未定为业务缺陷：本角色只加入期望/实际ID、索引、状态文本与 `failure-combo-selection.png` 的失败诊断，待测试重编复跑后区分鼠标定位假设与真实GUI表现。未修改业务或CMake，不将其余用例记为通过。

随后诊断日志 `build/shared-user-0.5-offscreen-diagnosis.log` 显示弹出项QTest鼠标输入后仍为原设备 `共享/工艺 `。本角色改为真实Up/Down键逐项选择，并等待每次scope异步请求完成；该路径两平台均通过。弹出项鼠标模拟没有选中，不据此将标准控件判为业务缺陷，也不声称本轮已验证所有鼠标选择方式。

键盘路径首轮日志 `build/shared-user-0.5-offscreen-keyboard.log` 到达批注CAS检查后失败，原因是测试只接受“冲突/修订”字样，而契约响应原文为“批注已由他人修改，请刷新已保存内容，确认草稿后显式提交。”本角色修测试为可读的他人修改/刷新语义，并让两客户端都走真实HTTP代理，额外核409/REVISION_CONFLICT；没有改业务文案或放宽冲突保护。

## 最终实际运行与数据证据

本角色在主管授权的独占构建时段，只使用已检查Ninja增量构建 `shared_user_tests`。最终offscreen完整日志 `build/shared-user-0.5-offscreen-semantic.log` 退出0，随后windows日志 `build/shared-user-0.5-windows.log` 退出0。两轮均输出：

```text
ANNOTATION_USER createExactScope=1 multiline=1 utcVisible=1 casConflict=1 refreshKeepsDraft=1 explicitRevision3=1 dirtyEscapeAndTitleClose=1
ANNOTATION_USER sameNameAndCrossEquipmentExact=1 deviceNullScope=1 authorPreserved=1
ANNOTATION_USER committedResponseLost=1 sameUuidRetry=1 laterDtoKeepsDraft=1 explicitResubmit=1
ANNOTATION_USER dirtyScopeProtected=1 wrongFilteredScopeRejected=1 missingPartProtected=1 wrongSaveIdentityRejected=1 oldCancelledCallbackIgnored=1
ANNOTATION_USER disconnectDraft=1 restartedPersistence=1 discardThenClose=1 noGeometryMutation=1 acceptance=1 simulated=1
ANNOTATION_HARNESS accepted=1 casPersisted=1 sameUuid=1 currentDto=1 wrongIdentityAndScope=1 oldResponse=1 restarted=1
SHARED_HARNESS cleanup=1 ownedTestTablesRemoved=1
```

真实代理确认两人旧版本提交返回409/REVISION_CONFLICT；首次新增已完成201后丢响应，再次原UUID原payload请求返回200，DTO已由另一客户端修改至版本2。Qt保留不同草稿并仅在下一次显式点击PUT后保存版本3。

服务pause→resume重建后，真实SQL检查：仍为两个精确设备ID、四个几何零件，批注正好三条。首条多行批注最终版本3，author甲、updatedBy乙及精确part保留；设备级批注part_id=null、版本1；幂等批注在错误响应注入前后仅一条，正常刷新取得真实已提交内容后最终版本4。响应篡改测试不声称远端写入撤销。finally清本轮四表后查询空库成功。正常8765 API、ev_shared、MySQL80和正常SQLite未操作。

## 原生截图独立查看与模拟结论

本角色独立查看 `build/shared-user-0.5-windows` 的09至17九张新增截图：

- 创建图显示中文引号多行原文，作者甲、版本1、明确UTC的ISO时间；选择后保存区和草稿区分别可见。
- dirty关闭图保留乙原草稿并明确提示先保存/放弃；冲突图红字说明他人修改，保存区仍旧原文，乙草稿完整。
- 刷新图保存区变为甲最新意见、版本2，乙草稿保持原样，提交按钮明确标版本2。
- 丢响应图新稿仍在、保存区为空；同UUID重试图保存区显示乙已更新的版本2，甲原草稿仍在。
- 错身份图红字拒绝不同批注/关联范围，旧保存区和新草稿并存；断服图保留草稿和已保存版本4并提示连接断开。
- 服务重建图显示原多行意见版本3、作者甲、更新者乙与UTC时间，草稿干净，与真实持久化断言一致。

正文、元信息、按钮与状态提示可读，令牌遮蔽，没有发现阻断交互的遮挡。批注列表长预览有水平滚动，完整正文在右侧保存区显示；不将预览截短当作内容丢失。本轮无需生产修复。最近200条外的选择、更多极端屏幕尺寸与跨电脑部署未由这些主情境验证，不扩大通过范围。

结论：0.5九项模拟需求及附加身份/取消场景通过，真实人工体验与独立代码审查仍分别记录。首次测试输入/文案假设错误保留为历史证据，没有包装成产品修复。
