# 当前工作状态与中断恢复入口

## 1.1 已交付（2026-10-10，当前状态）

1.1.0 已完成实现、全量自动门槛、原生实机验证、独立审查与打包：源码提交 a5fd782fc48ea4ef8fd095434ea4e906d38b2dc2（打包时工作区干净），分支 codex/desktop-assembly-1.1，标签 v1.1.0，main 同步该提交。正式入口/Build/Package/Run 已统一 build-1.1，旧 build/bin 与 1.0 包保留。

交付包 equipment-viewer-1.1.0-20261010-135556（ZIP SHA256 E01C361420E3140696DAFAF6F695A2EC3030E8533362EE6F6A1740498F411F0E；主程序 SHA256 69CDEF1A0F61A61BEF249738CADF4C33F07642ED81EDF3B9AF54A99B4B404682），511 文件，无数据库与凭证，含真实装配样例与 CC-BY-3.0 许可。全量 18 项 CTest 通过 32.07 秒（含 cad_import_process 9.90 秒与独立模拟用户 harness 0.93 秒），CAD 转换器独立 17 项通过 25.343 秒，独立 harness 原生 FBO 实际 samples=4、8×4 加载销毁后自有 QObject 全部回收（不声称零泄漏）。

包内 CAD 运行库完成真实 STEP 原生导入：六件电机 4162 顶点/4332 三角、82 件装配 157558 顶点/175598 三角，四种视图、世界 XYZ 四位置、有序展开、单件拉远、双击空白清选中保隔离、显示全部退出、数据库重启后源参数逐字段一致；包内 EXE 原生冒烟启动通过（临时库）。正常数据 data/records.sqlite 与 f01-acceptance.sqlite 未修改，升级前在线备份在 .recovery/databases/20261010-004600-562654（quick_check=ok，未声称恢复验证），旧运行库保留在 build-1.1/bin/cad-runtime-v1.0-preserved-20261010-131853，后端 API/Redis/MySQL 仍为 1.0 服务。真人满意验收待用户实际体验，不能以自动/模拟结果代替；未验证其他机器、长期复杂装配性能与制造级标注。

## 恢复过程诊断（历史记录，已由上方1.1交付状态取代）

截至当前：Scene signed边缓存B2E8/新test184CF普通cube12棱、反向winding18棱；r07比较前关物理grid保严格几何像素，已通过。六轮构建后其他17个CTest通过，CAD process门禁尚未关闭；正常exit0/normal1仍观察到本Job livePID，QueryFullImage权限5、ToolhelpSnapshot不再含该PID，UI调查查询时间竞态，未放松Active0门禁；NoWindow显式flags0x8080400没单独解决，不把console假设写为确定原因。

真实native6专项此前旧rawtriangle-edge版通过，新过滤边native6待复验。native82严格Qt导入已证JobActive0+exit0成功，但Controller保旧EQ001，真实错误CAD实体33共线退化三角。独立可信worker完整82/157558/175647成功，JSON在忽略.runtime/cad-tests-1.1/ventilator.json；仅供诊断，不能绕window原STEP导入验收。graph现在额外负责converter零面积三角诊断/最小受控tessellation修正，不删Solid/Face、不松严格parser；UI仍负责CadProcess竞态与真实诊断。root独占构建/native；review只读修正并保模拟tests。日志build-1.1-cad82-final.log、cad-diagnostic.log、ctest-sixth.log保留真实失败。

下一步：两阻断修复→增量受影响测试→真实STEP6/82与原生harness→必要全量→独审→干净源码commit→独立1.1包→main/tag/开发分支GitHub。暂无1.1正式包/远端main更新/真人验收，不以功能存在记已交付。

## 正式1.1装配查看集成（2026-10-10，当前优先）

用户明确要求将独立v0.3 Demo合并正式软件，之前仅Demo/不替换限制不再约束此次正式集成。主树新branch codex/desktop-assembly-1.1，自111e8bb干净基线开始；旧1.0标签/包与独立Demo包保留，正常SQLite/MySQL/API不作测试数据目标。

计划原生Qt集成：默认彩色/工程轴测/二维正交/同比例三视图，世界XYZ角落/模型旁/同时/隐藏，网格及默认关闭尺寸边缘读数；沿原包络中心auto/XYZ有序外向展开，选中件可控拉远/复原，稳定自动色与自定义；隔离时双击空白清选中保持隔离，清单双击或换有效件切隔离，显示全部明确退出。原设备/STEP/共享批注继续可用，源参数不能因演示位移写回库。

assembly_state唯一负责SceneView及assembly_scene_tests；assembly_ui负责Window/Controller/main/CMake/窗口回归与82STEP样本；assembly_review只读业务独审+独立模拟用户harness；root负责公共文档/串行构建/实际运行/包/Git。先冻结Scene接口，再并行实现。新文件和变更脚本先读再执行。接口与Scene/Window/Controller/1.1版本/控件/82STEP/三项新回归已落盘；首轮scripts/build.ps1 -BuildDirectory build-1.1 session79504编译失败，Scene projected里误放return result(256)，graphowner修正并处理独审03/04线视图/高亮/双击拖动边界。日志build-1.1-first.log。尚未技术验收/包/远端发布/真人验收，勿记完成；root独占构建，roles可由followup唤醒。


更新日期：2026-10-09。实际工程：D:/作业/简历/工作模拟。


最新实施恢复检查点：二构建118/118链接通过，CAD runtime首次stage rename WinError5；既有sync工具-Offline重试成功到build-1.1/bin/cad-runtime，失败stage保留未删。initial CTest16/18（checkbox中心测试及R07dims前提）失败已按真实Space/显式开启修；新incremental全编译通过、第二CTest仍16/18（cad_import_process一次受控结果失败、独立test全显隐后缺focusVisible）。cad_import_process单项5.61秒通过，原因未确定，不宣称已修业务。独立harness新增focusVisible对齐用户“显示全部”入口，final test D824待编；Scene09E371释放时确认双击/切模式取消已静审，headerC042纯中文Doxygen注释；新CAD test58FB真实--assembly-preview-check可用（临时库/6或82/截图）。接下来毫米网格步长标示对齐、最后增量/fullCTest+原生专项/6&82STEP、独审→源码检查点→1.1包→Git远端。尚未完成1.1交付，不把16/18记全过。

## 当前目标与授权

用户要求全部Agent开发，按0.3→0.4→0.5→0.6→0.7→0.8→0.9→1.0连续迭代、模拟用户反馈、独立审查和测试，并推送GitHub。0.7/0.8来自用户最新实际体验反馈，见docs/user-feedback-0.7.md。不等待逐功能实现或确认；技术检查、模拟验收与真实验收分开。允许下载必要依赖与本机独立服务，不包含付费或公开部署。

有效规范docs/engineering-standards.md v1.1尤其第12节；路线docs/release-plan-1.0.md。保留正常数据、用户改动和远端历史，不force/reset --hard/clean。凭证、数据库、runtime、生成物不提交。

## 已发布检查点

0.2功能8ebe748/文档3bb65dd；远端初始历史已由1b6ba2b保留合并。0.3为03c0c1a，0.4为aef7499，0.5为d3d720e，均已推送main/feature及各版本tag。最后ls-remote已核main/feature/v0.5.0 peeled均d3d720e8a063a4a24d70fa64c924ed9dfce1ffe8。分支feature/release-1.0，origin=https://github.com/GreenLoong/equipment-viewer.git（私有）。Git单次safe.directory=D:/作业/简历/工作模拟，不改全局。

包位于原聊天outputs独立时间戳目录；0.4 manifest对应aef7499、dirty=false，已清Qt环境原生smoke及独审通过。证据见docs/releases/0.4.md和review handoff。真实用户仅验收环境/记录与F01-1，后续不伪造用户验收。

## 1.0已完成交付（本节优先于后方保留历史）

2026-10-09最终：0.8、0.9、1.0已按顺序分别推送。v1.0.0 peeled7f6fcadb2bd06fbaad8f689570eb152c37803a2a；发布记录推送时main/feature均c402c76675596ebcc561fb8b9f4ca557f94466eb，atomic push及后续ls-remote退出0。本文属于后续恢复记录，若主分支再有文档提交以git log/远端为准，标签与包源码不变，禁止force。

最终包outputs/equipment-viewer-1.0.0-20261009-110210(.zip)，manifest1.0.0/7f6/clean/noDB；EXE65E9BAF494575310D403DEE0AC956278D6AB53B5695B25F299D62F686C9EC79D，ZIPC4F6E9ECDF828871769241CEB5A84D708BD9412424378009348C4E1C0C629C69。独立510个ZIP文件/511条目与目录逐hash等，474 runtime与0.9相同、manifest473吻合；source/运行库/包/原生启动/一次最终包内CAD及临时恢复均通过，日志与执行主体见releases/1.0.md和各handoff。preview outputs/1.0-CAD-preview.png为真实02装配图原字节副本，SHAF487F5FE36428872AB2483A642ACB5A2219B5E602D2DE3680336CCF2BA4BFD04。

当前无已识别交付阻断，真实用户仍只验收环境/记录/F01-1，新版真人体验没有伪造。Node有限GC堆递增约0.5MiB/没有ASan/跨机/Linux桌面/长期复杂装配未验证；源码和技术验收并不保证零泄漏。三条Agent非阻断建议（属性可读性、业务错误原因、聚焦薄件）留作1.1候选，不自动无限扩展。

接手：先核Git干净状态和实际服务。最近API1.0.0前台session77296/PID69664/8765，MySQL项目3310/PID70128、Redis桥6380/session82732/PID64612；health/mysql up、鉴权cache ready/inFlight0已核，只有只读未知UUID诊断无正常资料插入。原MySQL80/WSL没动；session不保证跨app重启有效，不要按旧PID盲停。CodeCPP/worker/SDK已冻结，后续实际反馈再派新任务并独审；已完成阶段不重做。

源码Git全部阶段检查点已保存并推送；正常records.sqlite在0.8升级前快照.recovery/databases/20261009-053034-777459，旧列demo3/equipment1/part3完全保留，F01另库未升级。ev_test和临时CAD恢复为受控验证，不是正常共享库已恢复；Git回退不得删除/覆盖资料。原独立Git工作树及legacy/runtime快照保留，不clean/递归删。

## 1.0前次集成记录（历史，最终状态以上段为准）

0.9已发布：main/feature最后远端5b9d4aea480578ce6e412bff615421fe39bfe837，v0.9.0 peeledfa82fd99656173d014562c16dcff175de165f613；atomic push与ls-remote确认退出0。包outputs/equipment-viewer-0.9.0-20261009-104701(.zip)，clean/noDB，EXE85339235AB0673EC9FEB3FA8D03EEAFD3477CC126235DD85D2DE29A584F84741；全部runtime与0.8相同、ZIP511条逐hash等目录，新主EXE清QtPATH原生smoke过及独立meta通过。Qt业务源未变；npm资源入口实际1/1/270请求过，heap+520568仍逐升不能称无增长。

现在1.0只统一版本/文档/正式交付，磁盘源CMake/main/Node/package/lock与新EXE均1.0.0。完整阶段0.3..0.9逐版实现验证/独审已完成；仅final source/包meta、模拟用户最终包试用与远端1.0待完成。真实用户没有再次验收。职责：root集成/Git/服务/公共文档，review_v1只读final source/meta，simulated_user_v1一次最终包试用；已停Agent须followup唤醒。没有新增业务、没有删正常资料或扩大部署费用。

1.0最新运行核对：主Qt EXE已重新编译通过（ninja主目标，无业务改变不重复未变化全suite）。仅向自己已确认旧API managed session23654发CtrlC，确认8765停止后新前台session77296/PID69664启动1.0.0；health/mysql up，鉴权status cache enabled/ready/inFlight0，miss1/set0为只读未知UUID404探针，无正常资料插入。MySQL3310/PID70128与Redis桥6380/PID64612保持，原MySQL80/WSL未操作。Source final review和包仍待完成，旧服务观察段为历史。

## 0.9前次集成记录（历史，最新发布状态以上段为准）

0.8已发布：main/feature最新远端91a83324e7723952e152667cb063fe4765b6a5e7，v0.8.0 peeled63806e0dd7fd50d6645b1aa4bb7c34dfe6731d8b，push与随后ls-remote均退出0。包outputs/equipment-viewer-0.8.0-20261009-055807(.zip)，EXE527A4793…43670，ZIP80202111bytes；全部源码/包独审通过，官方440库存、473 manifest/474 runtime集合、ZIP511条目逐hash一致。默认main README已0.8，历史保留，无force或部署工作流。

现在源码版本0.9.0，磁盘EXE尚0.8，正常API仍0.7/session23654。新增资源测试FF78已独审并实际270真HTTP/5批关闭资源通过，heap+517648逐批增长仅观察；root把npm test:resources加入入口，version/meta/现有恢复与有限资源证据将形成0.9检查点。Qt/worker/SQL业务没有新变化，不扩大为生产稳定/零泄漏保证。0.9元数据/原生包核对后推送，再1.0整合。独立reviewer及模拟用户用followup恢复，已停角色不默认存活。

## 0.8前次集成记录（历史，最新发布状态以上段为准）

最新门槛：新canonical runtime .runtime/cad-distribution/cad-runtime已prepare，旧pip目录未动。完整scripts/build最终16CTest23.75秒通过（build/build-0.8-final.log），模拟用户42EA测试默认新runtime两平台CAD+临时SQLite onlinebackup/restore/新Repository网格指纹全部接受，完整新实屏图在build/cad-user-native-default，原裁切图不作证据。正常API运行仍0.7，源码磁盘0.8未发布。

runtime独审发现R08-R01：未知unchecked-hash字节码缓存可以覆盖已核源码。主管最小删除verify缓存白名单，setup新SHA0ABD3A067E1250B7014984C7ACBF090544E24707398C897686413FD013EB9BA6，sync66D94479未改；新empty build/bin/cad-runtime同步成功，旧6F目录保存.recovery/runtime/20261009-055427/cad-runtime；同fixture独证待关闭才发布。review_v1独审此边界及0.9新Node资源test（FF78D39E，5批15cycle270真HTTP，活动socket/task/listener/libuv回基线、heap+517648仅观察），源码业务及U01/W01已通过。backend_stability两文件已落盘、实跑6.64秒过，待独审不称已完成0.9。

最新恢复检查点：主树已合入CAD数据c06bbb9/b8acea2/09225fe/b9c8ae5、图形118e1b4、UI10c0236与Job完整退出修正dd149bc。版本文件暂设0.8.0，尚未发布；GitHub稳定仍0.7。worker88735E2C独审14/14及原mixed负例通过，R08-W01已关闭；受支持电机改Yellow六闭实体，旧NEMA另含开放壳必须拒绝，不交付部分模型。

本地检查点cbdc651保存完整CAD worker/支持样例与版本文档；.gitattributes将STEP标binary，Git对象SHA与下载原文一致，禁止换行转换改变源身份。8ca9a11仅增加self-delete测试的通知/error诊断，业务没改，原失败日志保留，单目标复验中；无新版远端发布。

两平台cad_user当前A639测试已accepted1：真孔/6位置/单双击隔离/标尺/失败及原NEMA/取消PID0/本地拒CAD发布/旧JSON/重启指纹；5轮资源记录均通过粗门槛，native warm759152640bytes/638handles→末778043392/649，峰806977536，非零泄漏证明。部分QScreen实屏图截断，sim负责修测试取图再验证；原生FBO专项实际完整且samples4。generic软件/native各5×20窗×5loads/40HTTP已逻辑基线回归，源码独审仍待最终覆盖。

正常records.sqlite在一致性快照.recovery/databases/20261009-053034-777459（两个库quick_check均ok）后实际新应用升级成功；与快照每旧列逐行完全一致：demo_records3/equipment1/part3，quick/FK检查通过。F01另库未打开升级。无正常模型或测试数据插入。新的ev_test MySQL恢复核geometry/UTC/CAS/幂等全部接受，6269bytes，SHA b59f19e2967ff36c6cbf6f5701b17cb42d4aae98be9de7b9463881b65ba6aa1b，manifest restoreVerified=true/normalDatabaseRestore=false，清理1；新的真实MySQL完整integration1通过。

主树诊断-only测试8ca9a11单目标重编/默认runtime实际复验退出0，原失败未复现、原因未定，不做无依据的业务改动；仍待最后全CTest及独审。simulated_user_v1修完整屏幕截图和临时CAD备份恢复测试；cad_ui_r08独占runtime两脚本与许可；review_v1独审实际差异。Node38及新MySQL1通过；CAD两平台真实专项通过。root负责集成/CMake/文档/Git/包。各已保存文件保留，正常资料迁移结果见上段。

## 0.8前期开发记录（下列为历史观察，当前状态以上段为准）

0.6已发布v0.6.0/30b5fcc；0.7已发布v0.7.0/3e3bb57，README已由旧0.2主体改为当前0.7并保留docs/history原文。0.7包outputs/equipment-viewer-0.7.0-20261009-001651，EXE80DD310C45247D04127A564FF003CE3B52BDBA110C49D93CD9EA70D05BD40086，manifest源码3e3/dirtytrue仅未来worker未提交；已独核，不含CAD代码/数据/秘密。主source0.7全14CTest16.50/nativeScene与Window退出0，实际FBO4，用户交互反馈已技术验收，非用户再次验收。

0.8数据树.runtime/r08-cad-data（codex/r08-cad-data）：提交a15d091/4595142/e23c952/62f2c64，R08-01读库累计预算前置、R08-02真实引用网格双边近似包络、R08-03 SHA终换行已修/独审接受。真实轴承与电机协议→SQLite→重启完整互通；正常库未升级，合并与真实迁移先快照。

0.8图形树.runtime/r08-graphics（codex/r08-graphics）：组合0.7和CAD数据；提交c600d19b94f238cc65f5ad48632a8692675e59c1，仅Scene、新tests/cad_scene_tests、target、handoff。孔背景/曲面/6件实际投影位置/拾取显隐/标尺及旧Scene/R07两平台通过、native4；独审进行。CAD UI树.runtime/r08-cad-ui：异步进程/STARTUPINFOEX出生Job归属2GiB/输出预算/取消owner生命周期/导入失败保护已落盘，初16CTest22.34过，Python3.14真转换及最后hash尚未完成。

worker主tools/cad及Python tests已落盘，原10tests22.878过；独审R08-W01混合闭实体+开放面被静默丢弃，backend正在最小拒绝不支持几何并补真实回归，未给最终通过。不能把probe或数据/图形通过写为应用CAD已完成。CAD当前设计仅本地STEP，不将网格假作box上传旧API。

私有CAD runtime已装.runtime/cad-runtime：官方签名PSF Python3.14.8 embed SHA A93ABE456AB01BD96D7A085B3CDB6566B3063F4241360D114142FBDB07F0A310；OCP novtk/proxy7.9.3.1 cp314 wheel SHA E5ED4CAF7C2982EBD8175B35358D2B5FA1A1CF7557CB7A94E87D6A5A0F1E697E，-I实际import通过。系统Python未改；模块site-packages子目录，源worker绝对路径通过EQUIPMENT_CAD_CONVERTER指定。对应cp312 runtime与已下载机械STEP在.runtime中保留，不丢工作。最终包CAD运行库及许可证root仍在准备，0.9/1.0尚未完成。

正常服务最近观察：MySQL3310 PID70128，Redis桥6380 PID64612/session82732，API0.7 8765 PID64948/session23654。用户已启动WSL，不重启全局、不操作MySQL80。实际操作前重核，凭证/数据库/runtime不提交。CAD各树非Codex管理工作树，Git分支代码检查点与已落盘文件保留，禁止reset/clean/直接递归删除恢复。

后续：worker修复独审→数据/gfx/UI各冻结并合入0.8→源与嵌入式完整CAD导入/显示/保存/重启/失败和旧数据迁移→0.8包/GitHub→0.9资源/恢复与1.0整合。source/scripts初执行须先读；各树build可独立、原生GUI焦点和ev_test/6380测试必须串行。

## 0.6开发历史（已发布，保留证据）

0.5已技术/模拟验收、独审、打包及推送。WSL已由用户启动，root明确Ubuntu-24.04 /bin/true退出0，没有重启全局。MySQL3310已按原标记恢复PID70128；Redis7.4.7项目setup已ready。0.6全部源码独审通过，pure38、真MySQL兼容1、真Redis TTL/坏值/LIST/6MiB原子删除/恢复1（backend和review分别亲自跑）、sim真实HTTP/MySQL/Redis/GUI两平台均accepted1/cleanup1。R06-01..03、backup kill抛错均关闭。当前打包与GitHub收尾，不提前记发布。

root完整0.6构建12CTest通过13.57s；Qt资源5批×20窗×5loads/40requests两平台实际逻辑基线归零，观察数据在build/resource-observations-20261008-182251-448c67b3和182658-8701af43，无持续明显增长，不是ASan。0.9原恢复成功证据保留，CLI新helper6fake/独审已接受，真实新代码复验再协调ev_test。

0.7隔离Git树 .runtime/r07-viewer，分支codex/r07-viewer，Scene提交c27ff641bd97bf6e4fdf6c6ff4cfed98541059e4已落盘，13CTest和原生实际FBO MSAA4通过，独审待。root在此树编辑Controller/Window隔离双击与按钮名，sim编写独立r07_window_tests；主工程Qt保持0.6，正式集成在0.6发布后。此CLI工作树不是Codex管理工作树，不用archive工具或递归删除；保留到集成和检查点完成。

0.8真实CAD准备：已从FreeCAD Parts Library固定f5ca01c下载轴承与电机到忽略的.runtime/cad-samples，CC-BY许可/作者/hash已记录；OCP novtk/proxy7.9.3.1仅二进制wheel下载校验后装入.runtime/cad-python/site-packages，未改系统Python。实际探针读取轴承1实体约22×22×7mm、电机6实体；绝非应用CAD已接入。公开模型等0.8再复制samples，0.6包不包含未支持CAD样例。

## 已发布0.5证据

批注后端、MySQL v1→v2增量迁移、CAS冲突、原UUID幂等重试、UTC与Qt草稿界面已落盘，版本0.5.0。后端pure20/20、真实ev_test integration1/1已通过；review R05-01至05修正源码复核接受。

恢复后shared_user_tests重编成功。独立reviewer_annotation_faults实际退出0，dirty关闭、缺part、错UUID/part、recent200和旧回调取消全部accepted=1（controlledHttp，非真实MySQL）。全量12CTest通过13.40s，pure20通过856ms。真实MySQL双窗口offscreen/windows均全部accepted=1/cleanup=1；首轮combo鼠标及提示措辞测试预设已修测试helper并保留失败证据，没有改业务。

主API旧0.4会话43076已受控关闭，新前台78001运行0.5.0，health/mysql up，ev_shared schema2。迁移前后eq/part逐项核对一致（正常共享库原本为空，非空无损迁移由ev_test验证）。已有一致性备份.recovery/sql/2026-10-08T00-37-09-996Z-40aa75a9，3780字节、SHA核对通过，restoreVerified=false；不声称恢复验证。backup R05-01写失败凭证文件归属修复已独审/替身回归通过。最终包独审和远端同步已完成。

## 负责人

root负责Qt集成/CMake/公共文档/Git/交付。backend_v1负责api及自己的handoff，当前仅准备Redis runtime工具，不启用缓存、不改版本、不占ev_test。simulated_user_v1负责shared_user_tests/run_shared_user与反馈，目前独占ev_test串行测试，不改业务。review_v1只读独审业务，拥有reviewer_annotation_faults及审查handoff。各文件单一负责人，构建串行。

## 服务与数据安全

最近核对：独立MySQL3310 PID70128；旧正常API78001已受控关闭，新0.6 API前台session63142，Redis桥前台82732。正常cache.enabled开启，旧秘密配置另存忽略的.recovery/config；health0.6/mysql up，只读未知ID404探针后status cache ready/inFlight0，无正常库测试插入。操作前重新核实PID/端口，不能假设session仍有效。原MySQL80的3306实例不操作。独立数据/标记在LOCALAPPDATA/EquipmentViewerSimulation；api/runtime配置忽略，不打印内容。

ev_shared正常共享库；ev_test只从空库取得本轮所有权，非空拒绝测试和清理，清理annotations→part→equipment→schema_version。不删除正常SQLite。快照在.recovery/databases，Git不恢复数据库。

## 0.6准备与后续

Redis官方7.4.7下载校验并在WSL /opt/equipment-viewer-redis/redis-7.4.7编译成功（MALLOC=libc、无TLS），未启动或接API。归档SHA256 c97e57b0df330a9e091cacff012bebe763c275398cf36ff44cdba876814b595b。WSL网络None，计划受控前台TCP桥接，不改全局WSL配置。规则见docs/cache-plan-0.6.md。

下一步：0.5真实模拟验收→独审→正常API增量升级→文档/Git/包/远端；再0.6缓存HIT/MISS/断开回源；0.9资源/数据恢复/包装；全部通过后1.0交付。不得只改版本号。

## 恢复与检查入口

先读AGENTS/规范/本文/handoffs，再核Git状态、实际进程与Agent存活；已停Agent须followup唤醒或重新分配。新/变化脚本执行前阅读。scripts/build.ps1 fresh配置、编译、Qt部署、CTest；原生OpenGL另验。package.ps1从CMake读取版本、独立目录不覆盖旧包、不含数据库/令牌。

每个已验证里程碑检查diff --check/cached/status后提交并推送feature/main/tag。无部署工作流这一前提变化时复核。额度突然中断不保证最后记录，依最近落盘恢复，不能保证刷新自动唤醒Agent。

## 0.5源码检查点与0.6环境阻碍

0.5源码提交d3d720e8a063a4a24d70fa64c924ed9dfce1ffe8已完成；独立源码结论通过。包outputs/equipment-viewer-0.5.0-20261008-131825(.zip)，EXE SHA256 177D203D03D6314DB694631FADDC42A0E9721AC27593F14C540087DFE029A629，清Qt环境原生smoke0，无敏感文件。manifest dirty=true来自未提交0.6预备工具，桌面包不含这些工具。正在最终独审包/远端同步，不提前记已推。

WSL2.6.3 --version退出0；--list --quiet挂数分钟已取消仅此次命令，显式Ubuntu-24.04 /bin/true在15s预算内挂起并仅终止自己child，无残留wsl.exe。Registry真实发行版Ubuntu-24.04，WSLService Running；未改全局.wslconfig。异步问题已询问是否允许重启WSL（会终止现有Linux任务），答复前不restart/shutdown/terminate，不把等待当批准。仍可继续0.6纯代码与0.9受控测试准备。

runtime review初读发现未确认child被从连接预算移除的问题，backend修复中，未经复审不启动。review资源测试与simulated-user的0.9受控备份恢复工具仍是下一阶段，不混入0.5发布。

## 0.5已发布（覆盖此前待发布状态）

0.5独立包核对通过，源码/ZIP/EXE哈希一致，目录仅应用运行库/样例/指南，不含数据/凭证。d3d720e已成功push feature、HEAD:main及v0.5.0。Git默认OpenSSL首次SSL_ERROR_SYSCALL，单次-c http.sslBackend=schannel -c http.version=HTTP/1.1推送成功，未禁用TLS或改全局；最后ls-remote遭临时握手失败，push输出成功与额外远端读取验证分开记录。

当前进入0.6，仍待WSL重启问题用户明确回答。下一阶段工具/代码可自主准备并测试；不得提前发布0.6或1.0。0.5交付入口/预览在原聊天outputs；git源码与数据备份均已保留。

最新远端复核：使用单次schannel/HTTP1.1再次ls-remote已退出0，main和feature均d3d720e8a063a4a24d70fa64c924ed9dfce1ffe8，v0.5.0 peeled同提交；前一额外读取握手失败已恢复。0.5远端一致已核实。

## 0.6/0.9最新落盘状态

0.5发布记录与0.6冻结契约的文档检查点cd15eda已推送main/feature，0.5tag仍d3d720e。新的@redis/client固定6.3.0已由backend检查后--ignore-scripts安装、audit0；API package/lock磁盘0.6，但主服务78001仍已加载0.5模块，不宣称缓存已启用。

runtime bridge未确认child预算修复pure5/5且独审接受；Python转发前增加/proc/PID/cwd严格归属，pureproc替身2/2，最终独审待完成。未运行setup/Redis/bridge真实环境，用户WSL重启问题仍待答复。

sim真实受控MySQL恢复脚本已exit0/accepted1/cleanup1：.recovery/tests/2026-10-08T05-30-45-888Z-c7cfdc59，6269bytes，SHA B8CAE4C023BE8DF999C95677164A1DFA9F4CE32738F91E4C6A7BE1A50BC99AAD；manifest restoreVerified=true/normalDatabaseRestore=false，ev_test已空。独审核证据接受，CLI pipeline失败后kill未确认退出的边界正在补强；未把它当正常ev_shared恢复。root未操作正常DB/原MySQL80。

root仅维护集成/文档/Git；backend做cache业务与必要工具补强，sim做独立cache使用harness及restore CLI故障测试，review做Qt资源测试与独立审查。新脚本首次执行仍先全文检查；ev_test与Qt构建须协调串行。1.0门槛未完成。
