# 0.8 CAD用户试用与窗口验收交接

日期：2026-10-09。负责人只拥有 `tests/cad_user_tests.cpp` 与本文；没有修改产品、worker、SQL、CMake、其他测试或正常数据库/后台服务。

当前0.9/1.0交付核对阶段职责已收窄为仅本文及主管指定包路径后的受控只读试用；测试CPP归主管冻结，不再修改或重复0.8整套验证。下方源码职责与运行记录为0.8历史。

R08直接CAD导入需求来自真人。本文的Source试用结果将来自Agent真实控件操作/自动断言，不代表用户本人再次验收；新增体验反馈若发生会明确为Agent试用，不混为原始真人意见，不无限追加业务阻塞1.0。

## 测试范围与参数

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

实际结果、log/截图/mesh计数/哈希及有限试用意见见下文。若薄小件保持整体相机而不易看清，只记录实际可见性/建议，不擅自改架构或发明CAD制造参数。正常模型/JSON、库与服务保持，无法完成的用例不得写通过。

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

## 最小增量真实复验完成

主管全文读取rawPython与capture、单目标81257退出0后，显式CPP `--python/--modules/--converter` 启动首次在preflight失败，日志 `build/cad-user-offscreen-backup.log`。只读确认输入及runtime6条路径都存在；通过PowerShell宽字符串环境直接提供受信三路径、不传CPP这三参数后，`build/cad-user-offscreen-backup-env.log` exit0，在线备份恢复实际通过。问题位于测试CLI把中文Windows路径UTF8写环境的前提，不是产品STEP或源模型失败，未改产品。可选CPP runtime参数仍为本测试已知限制，外部宽字符串env或用户默认运行库可用。

主管默认canonical纯wheel runtime同步并完成完整构建46456退出0（16CTest23.75秒）后，本角色清 `EQUIPMENT_CAD_*`，不配置系统Python，亲自默认入口offscreen→native必要复验，两轮exit0：

- `build/cad-user-offscreen-default.log`
- `build/cad-user-native-default.log`

两轮均重验CAD_USER全部行为，轴承1444v/1428t、装配6件4162v/4332t/6中心；并实际输出 `CAD_SQLITE_RECOVERY onlineBackup=1 restoredNewRepository=1 meshAndSourceFingerprintExact=1 temporaryOnly=1`。源Qt连接仍打开，私有Python只读onlinebackup→新临时snapshot→新临时恢复库，quick/FK断言和新Repository完整网格/名称/材料/source/尺寸位置指纹一致。没有恢复正常数据库。15秒/退出确认等故障路径只作为代码保护，不声称本轮故意制造其超时故障。

不重复5轮资源，保留首轮原始资源数据。最新测试源仍42EA8E…E8D6；新的默认runtime实际可用，不因测试可选参数问题阻交付。原生72013已确认退出0并向root释放focus。

## 修正后十张原生图实际查看

本角色逐张查看 `build/cad-user-native-default` 01至10；新图完整1320×820，无旧大片白/黑裁切。capture日志available=(0,0,1920,1032)、frame=(16,16,1320,851)、DPR=1，frame/client完整落入屏幕，Scene背景ROI检查通过。截图来自真实桌面客户区，未合成GUI；FBO断言另作几何/像素证据。

- 01真轴承端面：通孔背景清晰、圆轮廓与环面橙色高亮；X/Y22毫米、Z7毫米与参考标尺，来源文件/完整SHA tooltip数据断言、全局包络非制造标注可见。
- 02完整装配：Metal/Plastic/Axis/Yellow/White/Transparent六件清单，真实曲面/分件位置可见，Yellow部分选中橙色、其他蓝色；bbox与来源属性存在。
- 03/04/05薄Axis：双击仅Axis可见，2×38×2毫米显示，旋转/缩放变化，回正后选择和隐藏保持；短投影明确线未显示。
- 06重复失败：旧Axis网格及属性仍在，状态为拒绝保存，设备目录断言无新增；不是partial CAD覆盖。
- 07取消后继续操作：双击改为Yellow唯一可见，界面按钮可用，真实worker PID0已在断言中确认。
- 08 CAD共享明确本地拒绝，未配置正常令牌也得到CAD原因，pending0且未发HTTP。
- 09旧JSON导入后重开CAD、10全新SQLite连接读回：六件及实际模型完整，选择Metal后属性/尺度正确。

默认入口、CAD行为、临时CAD SQLite一致性备份恢复与完整原生视觉证据通过。真人需求R08与Agent试用/自动断言来源保持分别记录，用户本人没有在本记录中再次验收；独审与最终包验证由主管另记。

## 有限Agent Source试用建议（非真人新增意见，非当前阻断）

1. 属性可读性：01 Z显示 `7.00000000000017`，02接近零中心显示e-15；长source名在属性区域需水平滚动。建议只优化显示精度/换行，保留持久化原值与完整SHA。这是界面呈现建议，不把真实CAD参数改成伪制造尺寸。
2. 失败提示：06重复源保护正确，但红字包含长CAD-ID与raw `UNIQUE constraint failed: equipment.id`；旧NEMA/badSTEP为通用exit1。可之后更清楚指出“文件已导入”或“不支持的开放几何”，不偷偷允许partial/OpenShell。
3. 薄件查看：03 Axis在整体设备相机下较细，04滚轮已能放大。可考虑一次聚焦选中件的查看便利操作；本轮不改架构/相机语义，不追加无限业务挡1.0。

三条已向主管明确为Agent试用建议，当前选中、隔离、标尺、回正、失败保护与默认导入均可用。没有从旧截屏白块推断或修改产品。

## 0.9/1.0恢复后的交付核对准备

已重新读取本交接和working-state优先的“当前进行0.8”节，并核Git范围。HEAD观察为63806e0（可重现runtime未知字节码拒绝检查点）；源码CPP仍SHA42EA8EB649A7B154ABD78F1E06109A1D61A7CD0C0549BA618C3C5EA9F062E8D6，Git diff没有该CPP修改。本角色只更新本交接，不覆盖review/releases/其他Agent资源测试的现有改动。

完成状态保持：真实公开轴承/Yellow6、默认runtime两平台、完整新原生图、同临时SQLite重开及onlinebackup/restore完整指纹已通过；旧预检失败/裁切图和有限资源计数均保留，不重跑整套、不删证据。三条Source可读性/错误文案/薄件查看建议为非阻断候选，不升级为新功能或阻止1.0。

当前由主管完成0.8包独审/tag/main，然后0.9压力与恢复正式入口、最终1.0包。未指定最终bundle路径前，本角色不运行其他包、主构建或原生窗口，不接正常库/后台服务。最终只做一次已协调焦点的actual bundle内runtime/默认入口原生试用，用独立临时数据库核CAD指纹和受控恢复，结果与源码0.8验收、真人再次体验、独立包审查分别记录。包路径、命令入口与受控输出位置由主管明确后执行，不推测未提供路径或声称1.0已通过。
