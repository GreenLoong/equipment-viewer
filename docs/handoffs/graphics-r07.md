# 图形工程师 R07 交接

工作树：D:/作业/简历/工作模拟/.runtime/r07-viewer；分支 codex/r07-viewer；基线 cd15eda。只在此树实施，主工程未修改。需求来源为 docs/user-feedback-0.7.md 的用户真实反馈。

## 已落盘

- src/sceneview.h/.cpp：水平拖动yaw反号，垂直/阈值/拾取保留；QOpenGLWidget首次创建前请求4倍MSAA，paintGL绑定真实绘制FBO后查询GL_SAMPLES并在rendererDescription报告。
- 中心截面标尺：40度垂直FOV、当前distance、视口高度与归一化millimetersPerUnit共同决定像素/毫米；1/2/5参考毫米值，明确垂直视线的模型中心截面。不是任意深度的制造标注。
- 选中可见件XYZ外包络线与毫米数值，圆柱X轴向、Y/Z外径。同色XYZ线和左上数值对应；投影过短/出界时只保留可解释数值，隐藏/空/错误场景不画。
- measurementAnnotations()为只读当前实际overlay注释副本，返回逻辑像素端点/实际long double毫米/线可见性，测试复用最终绘制数据。无SQL或Controller业务状态。
- 归一化最小修正：尺寸先转long double再除二保留double次正规值；计算millimetersPerUnit不窄化double；每件与实际float顶点比较跨度，失精度超过1%或坍缩则明确范围错误，不给虚假标线。
- tests/r07_scene_tests.cpp与唯一新增CMake target/test：已知100/80/60尺寸/各轴实际投影，拖动方向、保持垂直/短点选、zoom/resize真实比例、圆柱外径、DBL_MAX外径、次正规值、巨小件失精度、隐藏/空场景。

## 正在验证

已读scripts/build.ps1全部内容后在本树执行fresh构建。未声明通过；独立审查待root安排，不能用实现者自查替代。未改版本、Controller/Window、数据库、服务或Git远端。

## 实际检查与证据（2026-10-08）

- Qt6.11.1/MinGW GCC13.1/CMake3.30/Ninja：scripts/build.ps1在读取后首次执行，fresh配置与全构建成功，初轮13/13 CTest通过16.63s。
- 加入真实绘制像素断言后首次CTest 12/13：1.5px抗锯齿标尺没有完全覆盖像素，旧25色差阈值过严；失败保留build/r07-ctest-final.log。改为75仍区分背景/橙色模型，软件与原生路径scale/XYZ实际像素均通过。最终全量13/13通过14.18s，build/r07-ctest-accepted.log。
- 原生命令：build/bin/r07_scene_tests.exe -platform windows --snapshot-dir build/r07-native-final；build/bin/scene_tests.exe -platform windows --snapshot-dir build/r07-native-regression；均退出0。真实FBO的GL_SAMPLES=4；context()->format().samples()=0，不把后者或requested写成actual。日志build/r07-native-final.log、build/r07-native-regression.log。
- 原生已目视核对：build/r07-native-final/r07-box.png、r07-box-screen.png、r07-horizontal.png、r07-zoom-resize.png、r07-cylinder.png、r07-extreme.png。100/80/60与圆柱100/40/40正确，极端外径3.5954e+308mm不窄化为inf，标尺随缩放/resize更新。
- 抓图路径发现：QWidget::grab()重定向绘制遗漏原生paintGL的QPainter叠加，build/r07-native-evidence与r07-native-repaired保留旧缺标线抓图。gl->grabFramebuffer()与QScreen::grabWindow()均显示实际标线；测试改用真实FBO抓图，不将旧遗漏图当作原生失败或通过证据。GL/QPainter混合绘制补beginNativePainting/endNativePainting边界，依据Qt官方QOpenGLWidget文档：https://doc.qt.io/qt-6/qopenglwidget.html。
- 软件最新：build/bin/r07_scene_tests.exe -platform offscreen --snapshot-dir build/r07-software-readable退出0；截图已目视核对，尺寸/标尺均可读。offscreen插件默认未枚举本机字体，首次软件截图方框保留build/r07-software-evidence；仅测试从WINDIR/Fonts/msyh.ttc加载系统字体，没有复制字体或修改产品字体。
- 测试仅QTemporaryDir与build证据目录，不访问数据库、正常服务或未知进程。

## 代码导读与集成

SceneData::annotations根据实际相机投影创建毫米标尺与选中件包络标注，overlay统一供软件/GL绘制；setParts保存mmPerUnit和包络并先做实际float精度保护；GlSurface构造请求MSAA，paintGL实测采样；eventFilter只改水平yaw反号。measurementAnnotations返回annotations同一计算的只读副本，用于有效尺寸/方向/缩放检查。

实现者测试和目视检查不替代独立审查，也不代表新的真实人工验收。真实反馈来源已准确记录；root负责独审、Controller/Window集成、0.7功能验收/发布、用户交付。当前没有独审结论，未push/tag/main。

## R07-01 独审修复进行中

独立审查发现原Scene圆柱半径乘cos/sin仍先以double求值：denorm_min半径与2倍长度通过包络检查，但径向点被提前量化，模型失真。按最小范围在乘法前提升radius为long double，四个圆周角点全部复用此半径；未改相机、投影、MSAA、公共接口或root的Controller/Window/CMake。

tests/r07_scene_tests新增unit半径1/长度2与等比例denorm_min半径/长度的真实绘制比较，裁去左上数值及底部标尺区域，比较模型/轴线每个像素；同时断言XYZ外包络毫米数值和归一化投影端点。修复前先跑新测试，实际differentModelPixels=92415并退出1，失败日志/两张crop保留build/r07-01-before。当前修复已落盘，局部Scene/Ninja构建和修复复验正在进行，暂不宣称关闭独审项。

R07-01修复实际复验完成：局部Ninja构建r07_scene_tests/scene_tests成功；软件与原生完整r07_scene_tests均退出0，正常/等比例次正规圆柱模型crop的differentModelPixels均为0。原生依然请求4、contextFormat.samples=0、真正绘制FBO actual samples=4；原scene_tests原生回归退出0。受影响CTest scene_r07_measurements与scene_interaction两项均通过3.71s；未将rootUI未提交范围记成此次修复验证。

证据：build/r07-01-software-after.log、build/r07-01-native-after.log、build/r07-01-native-regression.log、build/r07-01-targeted-ctest.log；正常/次正规模型crop PNG在前两个after目录，原生次正规crop已目视核对。修复前的92415差异与原独审临时探针（59153不同像素，不同crop）均保留，没有替换失败证据。修复行为需要独立复审，当前仅实现者技术复验完成，不自行关闭R07-01或宣称0.7发布/真实人工验收。root未提交Controller/Window/CMake/window_tests未编辑、暂存或提交；本轮没有数据库/服务/CAD操作。
