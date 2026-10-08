# 0.7真实用户反馈窗口验收交接

日期：2026-10-08。工作树：`D:/作业/简历/工作模拟/.runtime/r07-viewer`。负责人只拥有 `tests/r07_window_tests.cpp` 与本文；未修改产品、Scene、CMake或主工程。

需求来自真实用户反馈，经主管冻结为0.7窗口行为；本角色使用自动QTest控件验证，不把这些需求写成模拟来源，也不声称用户本人已再次验收。CAD0.8不在本测试范围。

## 已落盘测试

使用独立QTemporaryDir/SQLite与三个同名虚构零件，ID分别R07-A、尾空格R07-B、R07-C；全部操作按精确ID关联。

- A隔离后单击B：A仍唯一可见，B只成为清单/属性选择；双击B才切为B唯一可见，选中B高亮。连续重复双击重建清单，每次重取坐标，不跨事件保留item指针。
- 搜索过滤掉当前选择只清选择/属性，隔离与显隐保持；过滤后的C单双击仍遵守规则，搜索保留。
- 失败加载保留当前设备/选择/隐藏/隔离；显示全部退出隔离保留选择；正常非隔离双击不自动隐藏。
- 手工隐藏剩一件不当作隔离，双击隐藏C不自动切显隐。
- fitAllButton文字为“视角回正”；实际画布方向和缩放改变后点击按钮恢复基准图，同时保留选择/显隐/隔离；选中圆柱标注X60/Y20/Z20毫米。
- 通过设备selector真实键盘成功切换设备后退出隔离，清显隐和选择。

按钮/清单单双击使用QTest；旋转的move与滚轮使用明确画布输入事件。截图只辅助视觉，状态与毫米值另有行为断言。尺寸/旋转/缩放并不涉及编辑几何。

## 当前状态与运行条件

主管已实现Controller `isIsolationActive()` 与窗口双击/按钮文案并统一编译目标，本角色亲自串行运行offscreen/windows，两轮均退出0。测试源保持SHA256 54E627462FE61E44BCC1A017F427BD60C24EB036074B879318B70737ADC5A57A，本角色未改产品或CMake。

截图补强已落盘：windows通过QScreen::grabWindow只截自己显示窗口，避免QWidget::grab丢原生QPainter标尺；软件平台仍grab。相机像素对照由sceneCanvas的公开QOpenGLWidget::grabFramebuffer取真实FBO（软件走canvas.grab），不以只有模型而遗漏overlay的画面比对。renderer日志来自实际绘制检测描述，不拿QSurfaceFormat请求4冒充FBO采样值；实际MSAA像素技术验证仍由图形负责人独立记录。

建议目标 `r07_window_tests` 链接equipment_window/Qt6::Test。主管构建后通知，再串行offscreen/windows；原生运行前与root/graphics协调焦点。主工程backend独占ev_test/6380与本树临时SQLite测试无共享数据，仍不抢原生焦点。

```powershell
build/bin/r07_window_tests.exe -platform offscreen --snapshot-dir build/r07-window-offscreen
build/bin/r07_window_tests.exe -platform windows --snapshot-dir build/r07-window-native
```

运行后更新实际日志、退出码、renderer、七张截图及独立查看结果。发现真实行为问题先保留具体步骤/期望/实际，不修改产品来绕过验收；测试预设问题另行说明。独立代码审查由review角色负责。

## 实际运行结果

主管目标fresh配置/编译64916退出0并释放树构建与原生焦点后，本角色执行上述两命令（进程PATH临时加入现有Qt/MinGW bin）：offscreen退出0，约1.99秒；windows退出0，约3.62秒。

日志：`build/r07-window-offscreen.log`、`build/r07-window-native.log`。全部状态字段为1：

```text
R07_WINDOW singleKeepsIsolation=1 doubleSwitchesExactId=1 repeatedDouble=1 sameNames=1
R07_WINDOW filteringKeepsIsolation=1 failedLoadPreserves=1 showAllExits=1 normalDoubleNoHide=1 manualOneVisibleNotIsolation=1 cameraResetDirectionAndZoom=1 statePreserved=1 selectedMeasurementMm=1
R07_WINDOW successfulSwitchExits=1 ... realUserFeedbackRequirement=1 automaticControls=1 acceptance=1
```

offscreen renderer为软件三维投影；windows实际绘制描述为“原生OpenGL（透视投影与深度缓冲）；MSAA请求4，绘制FBO实际samples=4”。相机回正像素比较使用真实FBO（含叠加）恢复基准，并分别验证旋转前后/滚轮前后不同。不把软件运行等同原生，不把请求4等同实际采样4。抗锯齿像素技术结论与水平旋转方向专门验证由图形负责人另记，本文件主要为窗口行为联动。

## 原生实屏七图独立查看

本角色亲自查看 `build/r07-window-native` 七张QScreen窗口截图：

- 01 A仍可见，B行标已隐藏且选中，属性为B的45钢/圆柱/半径10/长度60；仅换属性符合单击要求。
- 02 双击后只有B圆柱可见且橙色高亮；A/C标已隐藏，精确B属性保持；X轴向60毫米、Y/Z外径20毫米文字可读。短投影Y/Z明确写“线未显示”，未画误导长度。
- 03 搜索R07-C仅列C，双击后C唯一可见并高亮，C属性70/40/30毫米；搜索仍在。
- 04 手工隐藏A/C只剩B可见，双击C仍显示C属性且标隐藏，B未被自动隐藏，符合非隔离语义。
- 05旋转/放大后画面方向与06回正图明显不同；06仍只有B可见且选中，隐藏A/C保持。“视角回正”按钮可见，真实标尺叠加含200 mm及中心截面参考说明。
- 07成功切回EQ-001，恢复三件可见、属性提示请选择零件、无旧选择；旧隔离没有残留。

正文、只读参数、按钮与标尺可读，没有发现阻断该窗口行为的体验问题。尾空格身份/隔离bool/相机FBO等不能单凭图片证明，已由实际控件状态与数据断言取证。没有新增产品修复，也没有把此前QWidget抓图限制写成产品标尺未绘制。

结论：真实用户0.7反馈中本窗口范围的自动验收通过；用户本人没有在这份记录中再次验收，独立代码审查另行。测试进程已退出、焦点资源已向主管释放；正常数据库/API/Redis没有操作。后续CAD0.8及完整交付包体验不在本结论范围。
