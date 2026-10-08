# 0.4共享设备库模拟用户验收

日期：2026-10-08。角色：模拟工艺工程师 Agent。反馈来自测试控件操作及运行证据，不是真实客户反馈，不代替真实人工验收或独立代码审查。

## 操作范围与执行状态

0.4八项模拟操作验收已通过。本模拟用户Agent在主管完成ApiClient审查修复、重构建和3310恢复后，亲自串行执行offscreen与windows harness，均退出0，并独立查看八张Windows截图。Node语法检查此前退出0；主管报告旧11项CTest通过。代码审查与真实人工验收独立记录，本角色不代为宣称通过。

`tests/shared_user_tests.cpp` 在同一测试进程内创建两个真正的 `EquipmentWindow`，各有独立临时SQLite文件、Repository与Controller。发布、刷新、下载均通过QTest鼠标点击控件，设备选择及字段输入通过键盘；不调用ApiClient业务回调冒充网络结果。

`tests/run_shared_user.mjs` 强制显式 `EV_TEST_CONFIG_FILE`，核对数据库 `ev_test`、端口3310、账号 `ev_app`、本机地址及空库；只启动本次ephemeral HTTP端口。异步spawn Qt进程使Node事件循环仍能处理真实HTTP/MySQL操作。关闭服务通过临时文件握手执行 `app.close()`，等待取消使用独立本机挂起HTTP服务并观察真实socket关闭。最终只清本次从空ev_test创建的part/equipment/schema_version，不碰ev_shared、MySQL80、8765正常API或正常SQLite。

## 验收情境

| 编号 | 模拟操作 | 预期与证据 | 状态 |
| --- | --- | --- | --- |
| SU-04-01 | 客户端A发布，B刷新并下载 | B可定位完整设备，材料、形状、尺寸、位置和ID与A相同 | 两平台通过 |
| SU-04-02 | 发布设备ID `共享/工艺 ` 与 `..`，含 `零件/支架 ` 和 `..` 零件ID、同名零件与引号材料 | query详情路径及下载保持中文/斜线/点/尾空格身份，不按名称关联 | 两平台通过 |
| SU-04-03 | A重复发布，B重复下载 | 明确拒绝覆盖；本地设备、选择及资料保持，MySQL只两设备四零件 | 两平台通过 |
| SU-04-04 | 在共享库输入错误令牌再刷新 | 界面可读错误，本地设备和已选零件保留 | 两平台通过 |
| SU-04-05 | 网络请求已真正到达挂起端口时，按Escape关闭共享窗口 | 对话框回收、真实socket关闭，父窗口选择保留 | 两平台通过 |
| SU-04-06 | 关闭本次真实API后再次刷新 | 明确连接失败，本地设备与选择保留，可继续操作窗口 | 两平台通过 |
| SU-04-07 | API关闭后以新Repository和窗口打开下载资料 | 独立连接读回两设备/全部零件，模型清单及属性可离线查看 | 两平台通过 |
| SU-04-08 | 父窗口已选零件时，在共享窗口按Escape | 只关闭共享窗口，不触发父窗口“清除选择” | 两平台通过 |

## 执行入口

主管统一新增Qt目标 `shared_user_tests`，链接 `equipment_window` 与 `Qt6::Test`。真实服务验收独立执行，不加入常规QtCTest。

```powershell
$env:EV_TEST_CONFIG_FILE = 'D:/作业/简历/工作模拟/api/runtime/test.local.json'
node tests/run_shared_user.mjs build/bin/shared_user_tests.exe offscreen
node tests/run_shared_user.mjs build/bin/shared_user_tests.exe windows
```

两次运行串行进行；每轮结束由harness清自己的测试表。截图默认分别在 `build/shared-user-0.4-offscreen` 与 `build/shared-user-0.4-windows`。配置和令牌只留在忽略的本机配置或本次临时文件，测试不打印秘密。

最终证据应包含Qt `SHARED_USER acceptance=1 simulated=1`，harness `accepted=1` 与 `cleanup=1`，以及原生截图的独立查看。offscreen通过只能说明软件控件路径；没有原生截图不宣称原生体验通过。取消测试的挂起端口是明确受控错误场景，不将其声称为真实API正常响应。

## 范围边界

两客户端指同一测试进程内的两套真实窗口及独立本地数据库；本轮不据此宣称跨电脑部署或两个独立桌面进程已经验证。批注、并发版本冲突与缓存不属于0.4验收。独立代码审查由专职审查Agent负责。

## 首次实际执行与测试修正

主管完成ApiClient修复、重构建和3310恢复后，本模拟用户Agent实际执行offscreen harness，退出1。日志已显示 `publish=1 duplicateProtected=1 modalEscapeParentPreserved=1`，随后在下载数据检查 `parts[0].equipmentId == equipmentId && parts[0].id == partId` 失败。没有继续运行windows；harness输出 `cleanup=1 ownedTestTablesRemoved=1`，本轮测试表已清理。

检查发现本测试错误地假定JSON首零件仍位于Qt列表首位。既有SQLite `loadParts` 使用 `ORDER BY id`，点ID `..` 应排在中文ID之前，契约没有要求桌面保持JSON顺序。本角色仅修测试按两个精确ID分别核对全部资料，并补强位置与不适用尺寸为空检查；未修改业务，不将此断言失败记为共享功能缺陷。当时暂停等待重编，后续实际结果见下节。

## 修正后实际验收证据

主管重编共享测试后，本角色重新读取harness守卫和修正的ID断言，再执行上方两条harness命令。offscreen退出0，用时约6.3秒；windows退出0，用时约8.35秒。实际日志分别保存在 `build/shared-user-0.4-offscreen.log` 和 `build/shared-user-0.4-windows.log`。

两轮均实际输出：

```text
SHARED_USER publish=1 duplicateProtected=1 modalEscapeParentPreserved=1
SHARED_USER secondClientRefresh=1 slashChineseTrailingId=1 download=1 localDuplicateProtected=1
SHARED_USER dotId=1 invalidTokenPreservesLocal=1
SHARED_USER pendingClose=1 realSocketClosed=1 modalEscapeParentPreserved=1
SHARED_USER realServiceStopped=1 disconnectPreservesLocal=1 reopenedOfflinePersistence=1
SHARED_USER acceptance=1 simulated=1
SHARED_HARNESS accepted=1 mysql=ev_test port=3310 serviceStopped=1 pendingSocketClosed=1 simulated=1
SHARED_HARNESS cleanup=1 ownedTestTablesRemoved=1
```

harness在Qt退出后查询真实MySQL，断言只有 `..` 与 `共享/工艺 ` 两设备及四个零件；finally删除本轮拥有的三表后再次查询数据库为空。两轮均输出清理成功，未操作常规8765 API、ev_shared、MySQL80或正常SQLite。

本角色独立查看 `build/shared-user-0.4-windows` 八张截图：

- `01-client-a-published.png` 明确提示发布成功，令牌以圆点遮蔽；`02-duplicate-rejected.png` 用红色提示“设备ID已存在，不能覆盖”。
- `03-client-b-downloaded.png` 共享列表显示模拟设备，提示已保存可离线查看；`04-client-b-local.png` 有两个同名零件、各自不同ID，以及box/cylinder模型。
- `05-invalid-token.png` 红色显示令牌无效，密码遮蔽；`06-pending-before-close.png` 显示正在读取，动作按钮和地址/令牌禁用。
- `07-service-down-local-preserved.png` 红色提示检查连接，动作入口已恢复；本地保留由同步操作断言确认。
- `08-offline-reopened.png` 断服务后的新本地窗口显示同名零件清单、模型与选中橙色高亮，属性为中文/斜线ID、引号材料、100/80/60毫米尺寸。尾空格精确保持由数据断言确认，截图不用于辨认不可见空白。

八张截图中文/英文可读，没有发现文字遮挡或控件截断。截图不直接证明socket释放或SQL清理，这两项依据真实握手和harness查询。首次测试顺序错误的记录保留，修正后没有新发现的业务缺陷。

## 0.5建议需求：让共享模型支持工艺意见与多人修改

以下是基于已操作界面能力提出的新增模拟业务需求，尚未实现或运行，不是0.4缺陷，也不声称已经观察到并发丢失。

SU-05-01：选中共享零件后记录工艺批注。实际0.4界面可发布/下载模型，选中零件只提供只读材料/形状/尺寸；共享对话框只有刷新、发布、下载。我查看同名支架并核对尺寸后，需要留下“复核装配间隙”的工艺意见，另一客户端应能看到。建议按设备ID与零件ID关联批注，文本支持中文、引号和换行，显示作者、时间与修订号；不改几何参数。验收时A给中文/斜线/尾空格ID零件新增意见，B刷新准确显示，同名的另一零件不混入该意见，重启服务后仍可读。

SU-05-02：同时修改批注时保留各自意见。0.4只发布不可变几何，重复设备明确拒绝覆盖；没有可变协作内容及修订号界面。引入批注后，两位工艺工程师可能基于同一旧版本分别修改，建议提交携带所读修订号，先提交成功后另一提交明确报冲突，不自动覆盖或重发；保留待提交草稿，允许刷新最新意见后由用户决定如何修改。验收应由两客户端读取同一版本，A更新成功，B旧版本提交冲突且草稿保留，刷新后看到A意见，基于最新版本重新提交成功。该情境需0.5契约冻结与实现后真正执行，当前没有冲突实测结论。
