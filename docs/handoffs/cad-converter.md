# CAD转换工程师交接

创建2026-10-08，最终更新2026-10-09。已按主管明确分工转为0.8 CAD转换工程师；API0.6保持冻结，不改Qt/SQL/CMake/系统Python/服务。仅维护tools/cad/convert_step.py、requirements.txt、tests/cad_converter_tests.py和本交接。

**当前有效状态：R08-W01已修复，原NEMA混合独立开放shell整份拒绝，先前6part结果属于不完整历史证据，不再作为成功模型。当前成功公开装配为Yellow_gearmotor_closed_candidate.step，完整拓扑检查通过6实体；主嵌入式3.14与host3.12分别14项通过，见末节最新hash。**

已读AGENTS/规范v1.1/working-state和docs/user-feedback-0.7.md的0.8冻结设计。私有OCP novtk/proxy7.9.3.1与公开模型来源/哈希由主管核实；本Agent已读安装的OCP初始化代码，并对真实绑定docstring核实STEPCAF Transfer、ReadFile后SetSystemLengthUnit、XCAF Shape/Location、源Name与AddOptimal API。

CLI原型已落盘：Python -I convert_step.py --input绝对STEP --output绝对JSON --modules绝对受信私有目录。协议严格equipment-cad-mesh/v1/mm/sourceName/basename/sourceSha256/parts，设备ID不由Python产生。XCAF名字模式开启、自动伪名关闭，显式累计reference/assembly/leaf location；每面TopLoc变换再减全局包络中心，反向/镜像绕序处理；未知材料未提供，不伪造制造参数。

代码含输入100MiB/1000实体/600k顶点/400k三角/64MiBJSON/循环装配/有限数值检查与阶段时限；Windows2GiB/120秒准确作业约束由主管Qt父进程实现，POSIX RLIMIT仅保底。partial排他创建完整fsync后不覆盖发布，失败只删自己的partial；源前后完整SHA一致才发布。

## 已落盘实现与实际检查

首次原型和新增测试均先读内容/AST语法检查再运行；全部运行使用已安装hostPython3.12.8及项目私有模块，没有pip下载、执行样例来源仓库脚本或改系统Python。

- 真实公开轴承原文首次CLI退出0，1实体、1444顶点、1428三角，源名称608ZZ_Ball_Bearing保留，解析外包络约22×22×7毫米、中心0；JSON104634字节。
- 真实公开NEMA原文CLI退出0，6实体、5709顶点、5510三角，JSON363979字节。源名称Aluminium rear001/Body001/Aluminum front001/Rotor/conector/pin保留；全局中心分别(2.15,0,4.8)/(0,0,20.975)/(0,0,37.225)/(0,0,32)/(22,0,6.5)/(21.35,0,6.5)。测试与独立STEPControl.Reader.OneShape全局BRep bbox路径逐一吻合，非手工猜测装配参数。
- 可供主管接入的探针文件在忽略的.runtime/cad-probes/20261008-233705-646/bearing.json与motor.json；不包含设备ID，part ID为CAD-PART-0001…，源SHA与原始文件相同。
- 独立测试脚本tests/cad_converter_tests.py由本Agent编写执行，**10/10通过，22.878秒**（初轮8项16.991秒），不是独立review通过。真CLI在Unicode绝对路径读文件/输出，不从input cwd取OCP/json毒模块；协议严格根字段、原sourceSHA、局部有限顶点与合法整数索引、真实源名、无源名明确实体0001，全部闭合实体signed volume为正。
- 轴承洞/曲面检查：>100顶点/多样圆周坐标，真实网格体积小于外包络盒85%，投影XY的非竖直三角没有封住中心轴。未以bbox替代CAD。
- OCCT生成的受控测试夹具与公开原文区分：嵌套90度旋转+两层translation导出STEP再CLI，验证center(98,211,303)/dims(4,2,6)，证明累积位置次序；轴承副本只改SI单位mm为m声明，输出22000×22000×7000mm，是真内核单位换算，非手工输出参数。
- 错误保护：坏文件/open face无实体拒绝且无输出；坏文件失败后同target合法重试成功；已有output/发布竞态不覆盖用户字节。真实OCCT低实体/顶点/三角上限回归及输入/序列化输出限额、非finite、NaN/超时拒绝只清own partial。生产上限仍100MiB/1000/600k/400k/64MiB/120秒；测试缩小上限验证分支，不声称已压测到2GiB。
- 转换器序列化每4096块及发布前也检查阶段deadline；native Read/Transfer/Mesh期间的严格Windows120秒/2GiB及进程树退出需主管Qt Job Object真实验证，未以Python检查冒充准确内核中断。

## 尺寸与离散精度边界

dimensions/position来自BRepBndLib.AddOptimal_s(globalSolid,useTriangulation=False,useShapeTolerance=False)解析全局外包络，不是mesh vertices bbox或制造标注。真实面离散deflection=max(0.02,min(max(dimensions)*0.002,0.25))mm，角度0.25rad；与mesh极值允许已知离散差。实测motor最大mesh极值缺量约0.0011857mm（Rotor Y每边），其deflection0.12mm；已告知数据审查负责人，Parser绑定不宜要求完全等同，也不能接受明显虚报巨大dimension。

下一步：主管/独立review读取冻结4文件，接入Qt异步/Job/本地持久化/真CAD原生截图及包。本Agent不修改API0.6、不把本地mesh发布成box，不声明0.8已完成GUI/人工验收。

## 最终冻结与接手入口

运行检查入口：`C:/Users/23871/AppData/Local/Programs/Python/Python312/python.exe -I tests/cad_converter_tests.py`，默认从工程内已安装私有模块与公开样例读取；可显式--modules/--samples绝对路径。tests所有复制/导出/失败夹具只在已核范围.runtime/cad-tests独立临时目录，结束已清理；保留上述.runtime/cad-probes供主管真实接入。

限定git diff --check通过，以下文件现冻结供独审，不与审查并行修改：

| 文件 | SHA256 |
| --- | --- |
| tools/cad/convert_step.py | E6B6A692DB74260EB8112C3425E5CAB6E851125344702B7421282F35562BBCB3 |
| tools/cad/requirements.txt | 09DC78713B5A49B275CE2E33EA23BFDE3A5567D1EA9B618E18CD3F12BC88EC11 |
| tests/cad_converter_tests.py | E0AD0E5E8C4C24CFF1A780976C4388EB0AEAC58268031609E9325DE8D899D517 |

尚未验证：Qt父进程Windows Job约束/强制超时与关窗、嵌入式Python包与其他电脑、SQLite网格持久化/旧资料迁移、真实OpenGL选择隔离和标尺。由对应负责人在0.8集成后验证，本CLI测试不能替代这些门槛。

## 独审R08-W01：禁止混合几何部分导入

独审实际生成闭合2mm box+X100开放plane，旧E6B6源exit0只输出box，证明10项旧测试未覆盖混合文件。主管授权只改worker/tests/本交接，保持CLI与成功协议不变。

- leaf不再用仅SOLID Explorer略过其余几何：递归COMPOUND/COMPSOLID容器，闭合SOLID保持BRepCheck有效检查，单独face/shell/wire/edge/vertex记为待归属几何；完整模型扫描后用全局located solids的TopExp.MapShapes拓扑membership检查。真正被实体拥有的同拓扑+location面/边线不误拒，shell/wire wrapper可逐子项核实；不按相同包围盒/坐标猜身份。只要还有不被实体覆盖的几何，整体UNSUPPORTED_GEOMETRY，无结果发布；全无实体仍NO_SOLIDS。
- 此严检初轮确实令旧NEMA成功测试失败，未盲放。进一步真OCCT诊断其6Solids外还有4独立开放SHELL，各250faces/ClosedFalse；全局实体membership下shell未匹配、face覆盖0/250。其局部包络5×5×27.0396，不能据此推断螺纹/材料。已告知Root/Review，设计不扩为shell支持；原NEMA留unsupported负例，不包装成完整6件。
- 主管选新公开闭合候选Yellow_gearmotor_closed_candidate.step（FreeCAD固定提交/来源/作者许可由主管维护）。严格全拓扑CLI在主.runtime/cad-runtime Python3.14.8及cp314私有OCP7.9.3.1成功，6parts/4162vertices/4332triangles，JSON292026字节，source SHA37d7fdea3520a5b04b353a24eb5ff844f1d1e19c77865a52ad26a415a8de59e9。源名称Metal/Plastic/Axis/Yellow/White/Transparent保留、材料仍未提供。
- 新完整装配中心约(0,0,0)/(0.85,-13.5,0)/(0,1,0)/(0,25.25,0)/(0,35.75,0)/(0,-5.039537,0)。可接入JSON在忽略.runtime/cad-probes/20261009-003919-452/closed-gearmotor.json；此结果没有独立非实体几何被省略，不是只数6Solids的探针。

实际修复复验：

1. hostPython3.12.8运行`-I tests/cad_converter_tests.py --assembly <Yellow候选绝对路径>`，**14/14通过，27.005秒**。
2. 主项目嵌入式3.14.8运行`-I tests/cad_converter_tests.py --modules <.runtime/cad-runtime/site-packages绝对路径> --assembly <Yellow候选绝对路径>`，**14/14通过，27.183秒**。没有更新系统Python。
3. 新真正OCCT混合box+独立开放face/line导出STEP均退出1/UNSUPPORTED，无半份结果/partial；已有target用户字节保持，随后同target用合法轴承重试成功。纯多闭合实体compound成功；实体拥有的重复face/wire拓扑回归允许；原NEMA完整拒绝。原洞/曲面/SHA/名字/单位/placement/限额/无实体/隔离导入检查保留。

tests新增--assembly可选明确成功公开样例；缺省只用标明的受控六闭实体夹具测试位置，不能把默认生成box夹具称为公开机械模型。真实成功装配验收必须给定Yellow文件。原NEMA始终负例；不改各模块6实体期望或数据协议。

现在冻结供独审/Qt重新运行（覆盖前表worker/test旧hash）：worker **88735E2CB496E28FAAB48E347A393B12C7546104240B8139A847978754064FB6**；test **C39ACB301B568E921EED5A5D1776DB72505842054508433152F82EBDC8679A8E**；requirements仍09DC78713B5A49B275CE2E33EA23BFDE3A5567D1EA9B618E18CD3F12BC88EC11。限定diff-check通过，未改Qt/SQL/CLI/JSON协议/正常服务。测试是实现者技术验证，独审修复复核与GUI/Job/包仍由对应角色完成。
