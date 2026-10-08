# 0.8 CAD用户试用与窗口验收交接

日期：2026-10-09。负责人只拥有 `tests/cad_user_tests.cpp` 与本文；没有修改产品、worker、SQL、CMake、其他测试或正常数据库/后台服务。

R08直接CAD导入需求来自真人。本文的Source试用结果将来自Agent真实控件操作/自动断言，不代表用户本人再次验收；新增体验反馈若发生会明确为Agent试用，不混为原始真人意见，不无限追加业务阻塞1.0。

## 测试已实际落盘，尚未运行

参数 `--bearing`/`--assembly` 输入公开STEP路径，`--expected-assembly-count` 必须显式传入成功候选实测实体数；不固化机器绝对路径。`--bearing-dimensions` 默认已文档探针的22,22,7，可用实际冻结值覆盖。可选 `--rejected-step` 输入旧NEMA等严格拒绝文件；不会拿旧NEMA partial 6实体称完整成功。

测试调用与主按钮同一公开 `EquipmentWindow::importFile`，路径由参数提供；转换期间Qt事件继续处理，不用Controller直接塞CAD结果代替导入。运行时只用主私有/打包runtime，通过显式 `--python`/`--modules`/`--converter` 或应用旁cad-runtime，绝不用PATH Python。运行路径绝对且存在才开测。

有限试用路径：

- 真轴承实体：验证源名/完整SHA、真实顶点/三角索引、局部坐标与世界外包络毫米尺寸；独立射线证明中心通孔/外环，画布旋转到端面后中心空拾取、真实外环鼠标拾取、高亮、来源/外包络字段与标尺，不只shape=mesh或按钮存在。
- 新闭合装配：按期望实体数、实际网格规模及多个不同全局中心核布局；真实清单点选/搜索、最大与薄小零件单双击隔离，旋转/滚轮与视角回正、隐藏/选择保持。
- 坏STEP、可选旧NEMA、不允许重复源文件均保当前设备、网格指纹、选择和隐藏；取消真实已启动/Job验证的自己的worker后确认PID0、窗口仍可单双击与回正。
- CAD共享用QTest点发布明确在本地拒绝，pending=0、不加载正常令牌；旧JSON仍可导；关闭全部第一连接对象后新SQLite连接读回网格二进制指纹和几何，再显示模型。
- native截图用自己QScreen窗口；像素/真孔/回正取QOpenGLWidget真实FBO，软件走canvas.grab，避免漏overlay。

## 0.9有限重复资源模式

同一可执行文件 `--resource-cycles 5`：1次预热后5轮新临时SQLite、正常真实轴承导入、失败STEP、销毁窗口与Repository。每轮检查worker PID0与QPointer回收，Windows记录本进程PrivateBytes/HandleCount（Kernel32读取，不给未知进程发信号）。对预热基线使用+32 handles/+96MiB的粗粒度异常增长门槛，逐轮记录实际值；不是零泄漏证明或worker峰值内存测量，独审更严资源检查另记。不新增庞大框架。

## 构建/运行入口与当前冻结信息

主管接目标 `cad_user_tests`，链接equipment_window与Qt6::Test；本文件不碰主构建。候选由主管冻结：Yellow_gearmotor_closed_candidate.step，6个闭合实体，当前probe4162顶点/4332三角，位置保留；旧NEMA另4独立OpenShell应整体拒绝。worker与UI独审尚未最终结束，收到源码build/goahead及原生焦点协调后才执行，不使用旧worker先给成功结论。

```powershell
build/bin/cad_user_tests.exe -platform offscreen --bearing <公开轴承.step> --assembly <冻结闭合装配.step> --expected-assembly-count 6 --rejected-step <旧NEMA.step> --snapshot-dir build/cad-user-offscreen
build/bin/cad_user_tests.exe -platform windows --bearing <公开轴承.step> --assembly <冻结闭合装配.step> --expected-assembly-count 6 --rejected-step <旧NEMA.step> --snapshot-dir build/cad-user-native
```

主runtime已由主管复制到build/bin/cad-runtime（Python3.14.8/OCP7.9.3.1），worker最新再同步；源码运行也可参数显式指向主.runtime/cad-runtime/python.exe/site-packages和tools/cad/convert_step.py。不混用旧.runtime/cad-python版本。

实际结果、log/截图/mesh计数/哈希/原生试用意见待运行后补。若薄小件保持整体相机而不易看清，只记录实际可见性/建议，不擅自改架构或发明CAD制造参数。正常模型/JSON、库与服务保持，无法完成的用例不得写通过。

## 首轮真实行为与有限资源结果

主管集成dd149bc并构建A639115A测试后，本角色亲自执行offscreen→windows，参数为公开 `samples/cad/608ZZ_Ball_Bearing.step`、`samples/cad/Yellow_gearmotor_straight.step`、expected6、旧NEMA作为rejected及resource-cycles5。两平台均exit0，日志 `build/cad-user-offscreen.log`、`build/cad-user-native.log`。

行为断言全部通过：轴承1444顶点/1428三角、独立中心无击中/环有击中及真实端面鼠标空拾取/环拾取；6实体4162顶点/4332三角、6不同全局中心；来源SHA/局部顶点与全局mm、选择/搜索/双击隔离/相机回正；坏STEP/原NEMA/重复源不变且设备目录不增加；真实Job活动期间取消确认PID0；CAD共享本地明确拒绝pending0；旧JSON与前窗/连接析构后同临时SQLite网格/名称/材料/source/尺寸位置指纹读回均通过。native日志是原生OpenGL、实际绘制FBO samples4。真人未再次验收，独审仍另记。

资源模式是1轮预热+5轮正常/失败CAD导入销毁，worker均PID0、窗口QPointer回收，非零泄漏承诺。offscreen句柄293保持；privateBytes预热14430208，末14835712，观察峰15458304。native privateBytes预热759152640、句柄638；末778043392、句柄649；观察峰806977536（相对预热+47824896 bytes），句柄最多+11，满足本测试粗门槛。没有宣称主进程一直下降或worker峰值已测。

## 首轮原生截图无效，视觉尚未通过

主管查看02图发现2640×1640只有左边约320像素窗口，其余大块白/黑。本角色实际读取同图确认，不能据此声称完整GUI/装配布局视觉通过。已有行为/FBO断言证据有效，但原生整窗图必须补。当前没有截图异常等于产品自身问题的证据，未修改产品或虚构自动布局故障。

本角色只修测试capture：核主屏availableGeometry，按其范围设置窗口大小/位置、激活等待并要求frame/client完整落入；用桌面本窗口客户区抓图替代高DPI winId捕获；记录geometry/DPR，并检测真实Scene区域背景存在，失效截图也落盘但返回失败，不合成图。测试窗口初次显示前置顶确保不被别的应用遮挡。无法fit时输出测试诊断，禁止改minimum/layout隐藏问题。新图待重建和必要native复验/独立查看。

## 最小临时CAD SQLite在线备份/恢复增量

已只在同一CPP新增最小helper文字：私有Python3.14标准sqlite3，以只读源URI在线backup到全新 `cad-backup.sqlite`，再onlinebackup到全新 `cad-restored.sqlite`，quick_check和foreign_key_check各通过后由全新Repository/Controller核完整既有CAD指纹。三文件只能同测试临时目录且目标不得存在，不读正常data；源Qt连接仍打开。子进程15秒期限、kill后3秒退出确认，未确认保留本轮临时目录，不删除正在写入的路径。无通用工具框架。

新CPP SHA42EA8EB649A7B154ABD78F1E06109A1D61A7CD0C0549BA618C3C5EA9F062E8D6，旧A639行为记录保留。helper脚本和截图改动已落盘，尚未执行增量；等待主管全文读generated脚本文本和capture、单cad_user target重建后再必要复验。root主build99618时本角色不争用。focus已在首轮native结束向root/review释放，后续需再次协调。
