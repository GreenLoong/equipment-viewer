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

已自查文件与接口，尚未执行编译、测试或原生渲染（由主管统一构建集成）。不能记为通过。下一步：主管构建后将编译错误/场景测试结果交回；原生窗口和截图单独验证，再独立审查。

## 限制

软件回退以面平均深度排序，相交几何遮挡不等同原生像素深度缓冲。软件拾取和原生拾取共用投影面插值深度。极大坐标跨度的小零件可能低于浮点显示精度；几何被归一化但原始属性仍以毫米显示。当前没做10000零件性能承诺；软件排序与绘制对复杂设备会较慢。无CAD导入、材质或工程精度测量工具。

恢复时检查工程实际文件差异，不从工作副本覆盖主管或审查后的修改。不推送、不操作数据库。

## 恢复后原生退出检查与最小加固

主管已报告统一构建、九项CTest和scene_tests原生路径通过；该结果来自主管执行。整合主程序在并行原生检查期间一次工具外层失败，尚未证明由应用退出或GL生命周期导致。

图形工程师独立串行执行原修复前主程序：`build/bin/qt_sqlite_demo.exe --equipment-view --smoke-test build/graphics-before-fix.png`，临时数据库，实际输出SMOKE PASS且`LASTEXITCODE=0`；没有稳定复现原失败，不声称定位根因。

静态检查发现aboutToBeDestroyed的lambda连接保持到基类析构期，派生成员先销毁的顺序使此连接存在生命周期风险。依据Qt官方QOpenGLWidget文档“Resource Initialization and Cleanup”示例（https://doc.qt.io/qt-6/qopenglwidget.html），cleanup应断开该信号。最小修复：保存QMetaObject::Connection，cleanup在销毁GPU对象前断开；initializeGL连接当前的新上下文，防重复连接。不改变模型、相机或业务接口。

scene_tests新增三次“顶层窗口拥有SceneView子控件、实际绘制、销毁窗口”生命周期回归，测试不接触数据库。新版本尚待主管统一增量构建与原生复验；不要把原版本串行通过记成修复后结果。
