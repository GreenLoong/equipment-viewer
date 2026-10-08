# CAD Qt导入交接

2026-10-09；隔离树 `.runtime/r08-cad-ui`，base 2f45fb1；仅本树实施，没有正常数据库操作。

已落盘：src/cadimportprocess.h/.cpp、equipmentcontroller.h/.cpp、equipmentwindow.h/.cpp、collaborationdialog.cpp、tests/cad_import_process_tests.cpp；CMake仅注册源文件及单一测试目标。复制的docs/user-feedback-0.7.md未提交。

接口：CadImportProcess::Config运行绝对路径（EQUIPMENT_CAD_PYTHON/MODULES/CONVERTER或appdir/cad-runtime），start/cancel/isBusy/processId/isConfined；120秒deadline、100MiB输入、64MiB结果、合并日志累计128KiB。Windows STARTUPINFOEX JOB_LIST在创建时限制2GiB整个Job，KILL_ON_JOB_CLOSE，started核实际Job归属，取消最多等待3秒；未确认退出保持busy和临时输出。析构先清回调。非Windows仅运行worker自身限制，未声称POSIX实际验证。

Controller::importCadResult严格Parser→Repository原子save→完整load后替换状态；Window::importFile支持STEP/STP、importCadFile/cancelCadImport/cadImportInProgress。按钮importButton/cadCancelButton、属性cadSource、positionGroup外包络中心、boxDimensions网格外包络非制造标注。CAD发布在构造基础API文档前明确拒绝。

实际检查：已读build.ps1后用独立build-cad-ui目录fresh配置及完整102项编译成功；源Python3.12明确环境路径的16CTest全部通过22.34秒。新测试为受控临时脚本/SQLite，覆盖正常、错误退出、无结果、输入/输出/log超限、超时、取消、拥有者销毁、Windows实际进程退出及孙进程Job清理、坏协议旧设备保护、重复SHA和CAD共享拒绝，exit0/accepted=1。后来增加脱离QProcess信号栈的排队完成回调及代际取消，直接completion销毁拥有者与完成回调重启导入已测试通过；最终嵌入式运行库全CTest复验进行中。第一轮设备数测试遗漏空库默认EQ-001，已修正确预设，未修改业务处理。

源测试Python：C:/Users/23871/AppData/Local/Programs/Python/Python312/python.exe；实际worker主工程tools/cad/convert_step.py；test本身用临时fake converter，不修改worker。嵌入式Python3.14.8：主.runtime/cad-runtime/python.exe及site-packages；已使用同一独立进程类实际转换轴承1实体/1444顶点/1428三角形、电机6实体/5709顶点/5510三角形，严格parser及临时SQLite保存/销毁/重开均通过。同次测试包含全部受控失败路径。

可复现命令：PATH仅本进程添加Qt/bin与MinGW/bin，QT_PLUGIN_PATH指Qt/plugins；EQUIPMENT_CAD_PYTHON/MODULES/CONVERTER分别明确指上述嵌入式exe/site-packages/主工程worker；`build-cad-ui/bin/cad_import_process_tests.exe -platform offscreen --actual-bearing <608ZZ_Ball_Bearing.step绝对路径> --actual-motor <NEMA_17_with_connector.step绝对路径>`。测试目标可只跑fake，需已有明确Python运行文件（环境变量或appdir/cad-runtime）；不依赖系统PATH自动发现Python。

复验发现并修复：排队完成回调与进程NotRunning之间存在极短间隙，Window最初只按process busy查询会提前解除业务忙状态。新测试真实捕获该间隙；Window现明确保留cadBusy_直到业务回调完成，所有导入入口拒绝重入。该次CTest为15/16，不能沿用其通过结论；最终修复并增加关闭窗口/混合stdout+stderr累计超限测试后，嵌入式3.14.8完整16CTest已全部通过21.79秒，单CAD流程4.07秒。取消期间实际消费剩余管道，不保留日志正文。

真实worker后续并发变化：之前两轮3.14.8真实轴承和电机均成功；最后一次轴承仍成功，电机却exit1并保留旧资料，已报告主管请worker负责人检查，未将此前成功作为最新worker验收结论。Qt源码不修改主worker，实际输入SHA与测试环境未变。

未验证：独立审查尚待主管安排，POSIX未运行；当前Scene只有R07，无mesh显示实现，画基础几何不能算CAD验收；组合gfx后root执行真实CAD原生显示。正常用户数据库未打开/迁移，真实用户验收未发生。Repository成功提交后完整load如果遭磁盘读取故障，旧显示保留但新有效设备已保存，提示明确“已保存但打开失败”，不能声称已回滚提交。worker混合实体与开放面的边界独审整改由worker负责人完成，不在本树范围。

## 独审后的进程树退出边界补强

63d15a4后独审指出主QProcess NotRunning不能证明Job内孙进程清空。已落盘补强：任何结果读取和目录释放之前查询自有Job的BasicAccounting.ActiveProcesses==0；有残留则TerminateJobObject并在取消统一最多3秒预算内重复查询，未确认保持busy/Job/temp和可读错误，拒绝后续start。不以Job等待句柄信号替代活动数。发现残留的转换即使worker exit0也作为失败，不读取输出/不导入。

新增仅受控Config故障钩子allowJobQuery/allowJobTermination，false只制造自身真实API失败，无PID/handle外泄、不能伪造active0；析构清故障钩子并实际清理自有Job。新Windows测试主exit0后真实孙进程持result/cwd，覆盖正常清树/查询失败/终止失败保留/retry恢复，临时库旧设备不变；已实际运行通过，不用旧16pass覆盖新边界。

新边界实际检查进展：已重编，Windows新真实父exit0/孙持文件与cwd、query/terminate故障保留/恢复、带故障状态owner销毁均实际通过。发现Windows主退出信号可能先于Job计数更新，只有一个累计进程时等待计数归零，累计创建过子进程且仍活动则终止本Job。首次强制子树回归还发现QTemporaryDir析构删除可能未成功，改为ActiveProcesses0之后显式remove成功才放行，按同一退出预算短暂重试，失败保留busy/job/temp。随后受控测试连续3轮exit0。

最终冻结Worker（主管提供88735）轴承/Yellow6真实流程exit0：轴承1实体1444顶点1428三角，Yellow6实体4162顶点4332三角，均严格parser→临时SQLite→重启读回通过。新实现完整16CTest实际全通过23.13秒（cad_import_process含新父退出后孙留存与故障恢复，5.56秒）；未运行CAD原生图形窗口。对析构时真实Windows API持续失败的极端情况只能有界尝试后RAII关闭KILL_ON_JOB_CLOSE并保留临时目录，不声称已验证整个系统异常时的退出。测试只模拟自身Job API失败，未扫描/终止未知Python。
