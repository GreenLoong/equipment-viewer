# 当前工作状态与中断恢复入口

更新日期：2026-10-09。实际工程：D:/作业/简历/工作模拟。

## 当前目标与授权

用户要求全部Agent开发，按0.3→0.4→0.5→0.6→0.7→0.8→0.9→1.0连续迭代、模拟用户反馈、独立审查和测试，并推送GitHub。0.7/0.8来自用户最新实际体验反馈，见docs/user-feedback-0.7.md。不等待逐功能实现或确认；技术检查、模拟验收与真实验收分开。允许下载必要依赖与本机独立服务，不包含付费或公开部署。

有效规范docs/engineering-standards.md v1.1尤其第12节；路线docs/release-plan-1.0.md。保留正常数据、用户改动和远端历史，不force/reset --hard/clean。凭证、数据库、runtime、生成物不提交。

## 已发布检查点

0.2功能8ebe748/文档3bb65dd；远端初始历史已由1b6ba2b保留合并。0.3为03c0c1a，0.4为aef7499，0.5为d3d720e，均已推送main/feature及各版本tag。最后ls-remote已核main/feature/v0.5.0 peeled均d3d720e8a063a4a24d70fa64c924ed9dfce1ffe8。分支feature/release-1.0，origin=https://github.com/GreenLoong/equipment-viewer.git（私有）。Git单次safe.directory=D:/作业/简历/工作模拟，不改全局。

包位于原聊天outputs独立时间戳目录；0.4 manifest对应aef7499、dirty=false，已清Qt环境原生smoke及独审通过。证据见docs/releases/0.4.md和review handoff。真实用户仅验收环境/记录与F01-1，后续不伪造用户验收。

## 当前进行0.8（本节优先于后方保留历史）

最新恢复检查点：主树已合入CAD数据c06bbb9/b8acea2/09225fe/b9c8ae5、图形118e1b4、UI10c0236与Job完整退出修正dd149bc。版本文件暂设0.8.0，尚未发布；GitHub稳定仍0.7。worker88735E2C独审14/14及原mixed负例通过，R08-W01已关闭；受支持电机改Yellow六闭实体，旧NEMA另含开放壳必须拒绝，不交付部分模型。

主树新增cad_user_tests已编译，CPP冻结A639115A；simulated_user_v1正在两平台真实轴承/Yellow操作与资源检查。最新完整CTest15/16通过、cad_import_process line175 self-delete等待失败，UI作者正在诊断，不能记全通过。Node纯回归38/38通过。cad_scene新Yellow软件位置/孔/拾取/标尺专项退出0。review_v1独审UI；cad_ui_r08独占运行库准备脚本，bootstrapping/许可尚待最终复审；root负责CMake、公共文档、构建、Git及包。主树未提交文件保持，正常SQLite未升级/无测试写入。

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
