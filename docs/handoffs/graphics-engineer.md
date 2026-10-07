# 图形工程师交接

2026-10-08，读取AGENTS、工程规范v1.0含第11节及working-state后实施。

## 已落盘

- src/sceneview.h、src/sceneview.cpp：固定SceneView接口，内部原生QOpenGLWidget和软件QWidget双路径。
- tests/scene_tests.cpp：零件拾取、选择图像变化、隐藏不可拾取、恢复、拖动防误选、缩放、查看全部、圆柱、极端有限值、非法及空场景；支持--snapshot-dir。
- 工作副本在共享聊天目录work/graphics；工程文件为后续集成权威版本。

## 接口和行为

SceneView(QWidget*)、setParts、setSelectedPart、setHiddenParts、fitAll、rendererDescription、onPartPicked，与主管固定接口一致。setParts复制几何并清空视觉选择/隐藏；调用方随后同步Controller状态。短点空白回传空ID。

Box为12三角面；圆柱20段、沿X轴，端盖及侧面共80三角面；Z向上。有限值先缩放、再求边界与归一化，避免DBL_MAX相加/相减溢出；低于显示精度的跨度报告场景错误。相机透视、近远面0.05/100、拖动阈值6像素、缩放距离2.5至60。

原生路径采用着色器/VBO单批三角形和深度缓冲；上下文销毁时清理资源。offscreen/minimal不创建GL控件，明确软件3D透视投影。GL初始化失败或有效上下文创建失败时可解释回退。

## 集成要求

CMake新增Qt OpenGL、OpenGLWidgets模块；应用及使用Window的测试链接Qt6::OpenGL、Qt6::OpenGLWidgets并加入sceneview.cpp。scene_tests需src包含目录、Qt6::Widgets、Qt6::Sql、Qt6::Test、Qt6::OpenGL、Qt6::OpenGLWidgets；Part头含SQL类型，因此Sql包含路径必需。

## 实际验证与未完成

首轮自查时尚未构建；现已由主管统一完成加固后的增量构建、自动检查与原生运行，具体证据见下方“最终模块交付状态”。图形工程师未并行运行全项目构建，真实人工体验仍待用户反馈。

## 限制

软件回退以面平均深度排序，相交几何遮挡不等同原生像素深度缓冲。软件拾取和原生拾取共用投影面插值深度。极大坐标跨度的小零件可能低于浮点显示精度；几何被归一化但原始属性仍以毫米显示。当前没做10000零件性能承诺；软件排序与绘制对复杂设备会较慢。无CAD导入、材质或工程精度测量工具。

恢复时检查工程实际文件差异，不从工作副本覆盖主管或审查后的修改。不推送、不操作数据库。

## 恢复后原生退出检查与最小加固

主管已报告统一构建、九项CTest和scene_tests原生路径通过；该结果来自主管执行。整合主程序在并行原生检查期间一次工具外层失败，尚未证明由应用退出或GL生命周期导致。

图形工程师独立串行执行原修复前主程序：`build/bin/qt_sqlite_demo.exe --equipment-view --smoke-test build/graphics-before-fix.png`，临时数据库，实际输出SMOKE PASS且`LASTEXITCODE=0`；没有稳定复现原失败，不声称定位根因。

静态检查发现aboutToBeDestroyed的lambda连接保持到基类析构期，派生成员先销毁的顺序使此连接存在生命周期风险。依据Qt官方QOpenGLWidget文档“Resource Initialization and Cleanup”示例（https://doc.qt.io/qt-6/qopenglwidget.html），cleanup应断开该信号。最小修复：保存QMetaObject::Connection，cleanup在销毁GPU对象前断开；initializeGL连接当前的新上下文，防重复连接。不改变模型、相机或业务接口。

scene_tests新增三次“顶层窗口拥有SceneView子控件、实际绘制、销毁窗口”生命周期回归，测试不接触数据库。主管现已在加固后版本完成统一增量构建及原生复验；结果与修复前的独立串行检查分开记录。

## 最终模块交付状态

2026-10-08，主管提供真实执行结果，图形模块实现、统一构建与运行验证完成；交付源码检查点为 8ebe748。本次仅更新交接记录，不修改实现或测试代码。

- 加固后的统一增量构建完成；九项CTest全部通过，7.49秒。
- 串行 `scene_tests -platform windows --snapshot-dir build/native-scene` 退出0，明确输出原生OpenGL；含三个父窗口绘制和销毁回归。
- `equipment_window_tests` 原生属性五步操作及 `qt_sqlite_demo --equipment-view --smoke-test ...` 均退出0。
- 独立包移除Qt PATH和插件变量后，原生smoke仍退出0；包内exe与build版本哈希一致，主管报告SHA256前缀 C111C6A。完整包证据和哈希由主管的最终验证记录维护。
- 先前Git检查因本机Git信任/目录环境未识别而未完成；主管已显式safe.directory确认真实D工程仓库并保存本地代码与记录，该问题不构成数据丢失证据。

旧并行原生检查的一次工具外层失败尚未稳定复现，也没有证明与已加固的信号生命周期同因，必须保留该事实。后续原生回归继续串行执行，若再现先记录子进程原始退出码及日志再定位。

本机原生OpenGL验证通过不等于其他电脑兼容性或复杂设备性能已验证。软件回退仍是平均面深度排序，相交几何可能有遮挡差异；未承诺10000零件实时性能，极端相对尺寸可能低于显示精度。最终真实用户体验/验收仍待反馈，模拟反馈和自动检查不得代替真实验收。