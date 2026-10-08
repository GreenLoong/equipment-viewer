# 0.8 CAD数据工程交接

2026-10-08；隔离树 `.runtime/r08-cad-data`，分支 `codex/r08-cad-data`，基线 `30b5fcc`。只修改授权数据文件及CMake源/测试注册；复制来的user-feedback修改保留且不纳入本人提交。

## 公共接口

- `MeshGeometry`：`QList<std::array<double,3>> vertices`、`QList<std::array<quint32,3>> triangles`，局部毫米坐标。
- `Part`追加`std::shared_ptr<const MeshGeometry> mesh`、`QString cadSourceName/cadSourceSha256`，旧aggregate初始化兼容。
- `MeshGeometryCodec::validate/encode/decode`均bool返回，可读QString错误；编解码失败不改变输出。`validateBounds(mesh,size,position,error)`核正尺寸、局部bbox容差和世界坐标有限。
- `CadResultParser::parse(bytes,equipment,parts,error)`静态接口；严格协议/字段/UTF8、SHA64hex、实体与合计资源、网格拓扑、bbox校验；成功才替换输出。设备ID为`CAD-`+完整规范化小写SHA，设备名为basename。

## 行为与导读

`meshgeometry.cpp`以EVM1魔法+两个little-endian uint32计数+doubleXYZ+uint32indices编码；QDataStream显式Qt6_5、LittleEndian、DoublePrecision。decode分配前计算64位长度并检查64MiB、600k顶点、400k三角限制，不使用压缩。校验拒绝非有限点、越界/重复索引、共线/零面积三角形；叉乘前坐标归一化避免大值中间溢出。

`equipmentrepository.cpp`扩展明确mesh CHECK及nullable BLOB/source字段；基本几何字段与CAD字段互斥。save同一事务保存几何/source。load完整decode/validate后返回。CAD合计≤1000件/600k顶点/400k三角，基础几何仍可≤10000件。

升级只接受30b5fcc精确canonical旧equipment/part定义；已知new canonical与SQLite重命名形成的quoted-table DDL也接受。事务外关闭/核查外键，BEGIN IMMEDIATE锁内建立`__ev_part_v2`，逐字段复制旧行含rowid，核行数，替换旧表，重建原索引/trigger SQL，quick_check及foreign_key_check，COMMIT后恢复/核查外键。失败ROLLBACK，保留库和原错误，不按行数补种。主/临时schema同名迁移表拒绝覆盖；不使用writable_schema。

## 实际验证

先全文检查scripts/build.ps1，再执行独立树fresh构建（Qt6.11.1 MinGW13、CMake/Ninja）。初次完整构建成功，13/13 CTest通过16.82秒；含原12项记录/CRUD/JSON/GUI/Scene/网络回归与新增cad_repository。

新增测试仅QTemporaryDir库：严格parser失败保留、EVM1长度/计数/NaN/索引/退化边界、bbox、mesh重启持久、坏BLOB拒绝；旧canonical含引号ID、box/cylinder全部字段、rowid42/99、index/trigger、两次initialize；name碰撞、TEMP碰撞、读锁制造COMMIT失败并核实DDL/数据回滚与外键恢复。补充后增量完整构建成功且无新增编译警告，最终13/13 CTest通过14.28秒，cad_repository通过1.45秒。提交前diff --check、cached、status已检查。

未访问正常SQLite；正常库一致性快照及实际集成由主管负责。没有自称独立审查、模拟验收、真实用户验收或0.8发布；尚未完成独立审查与实际CAD/GUI集成验证。该树旧WSL工作状态不作为当前阻塞。

## 真实转换器互操作续验

读取backend实际OCP转换结果（20261008-233705-646的bearing.json和motor.json，仅忽略的runtime文件）。协议v1/unit mm/sourceSHA64、CAD-PART-0001起的ID与冻结接口一致，无需调整任一模块API。

测试新增可选argv probe文件入口，源代码不含机器绝对路径。实际命令：`build/bin/cad_repository_tests.exe <bearing.json> <motor.json>`。2026-10-08重编该target成功，执行退出0：`608ZZ_Ball_Bearing.step parts=1 vertices=1444 triangles=1428 accepted=1`，`NEMA_17_with_connector.step parts=6 vertices=5709 triangles=5510 accepted=1`，`ParserCodec=1 Persistence=1 Migration=1 Interoperability=1`。

真实结果经CadResultParser解析，存入本人QTemporaryDir临时SQLite，销毁原Repository后用新连接initialize/load，逐件比较完整ID/name/material/shape/position XYZ/bbox XYZ/source名SHA以及所有double vertices/uint32 triangles，均精确一致。重复SHA设备保存明确失败。bearing外包络22×22×约7mm/centre0；motor六件真实名称及非零placement保留。没有修改worker、正常数据库或主树数据接口；该技术互操作不替代原生CAD画面、模拟体验及独立审查。

## R08-01审查修复：加载前核合计预算

独立审查指出旧loadParts直到全部decode后才核合计资源，外部/损坏库多条各自合法网格可能先导致大量分配。最小修复：主查询仅取BLOB长度与12字节EVM1头；每行先核总part≤10000/CADpart≤1000及合法头/64位期望长度/单条限制，再累计vertices≤600000、triangles≤400000。只有预算内当前行才通过参数绑定查询完整BLOB并decode，读回BLOB头/长度必须与已核结果一致；超限即返回，不继续读后续行。公开接口和既有失败输出语义保持。

受控回归只用QTemporaryDir：3行各自合法200001顶点BLOB（每条约4.8MiB），总600003，第三行在decode前报告合计超限。再将第三行payload首double改NaN且保留合法头/长度，仍优先报告合计错误，证明不进入第三行decode分配；不制造OOM。完整增量构建成功，13/13 CTest通过14.84秒；真实bearing/motor跨模块重启存取复验退出0，Budget=1/Interoperability=1。正常数据库未访问，修复待独立复审。
