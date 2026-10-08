# R08 图形交接记录

更新：2026-10-09。角色：CAD 图形工程师。隔离树 `D:/作业/简历/工作模拟/.runtime/r08-graphics`，分支 `codex/r08-graphics`，实施基线 `2f45fb1`。本记录仅包含实际技术验证；本改动尚待独立审查、主管集成和真实用户验收，没有宣告 0.8 发布。

## 修改范围及实现导读

- `src/sceneview.cpp`：明确 `shape='mesh'` 分支，按 CAD mesh 三角索引真实绘制；世界坐标为 `part.position + localVertex`，复用既有 long double 归一化、透视/深度绘制、ID 拾取、高亮与显隐。mesh 不进入 box/cylinder 生成分支。
- Scene 直接入口重复检查 mesh 有限数值、三角索引、源外包络一致性及世界溢出，并检查全部 mesh 合计顶点≤600000、三角≤400000。错误清空旧三角/包络/选择，显示可定位错误，无旧模型及假参数标线。
- CAD 源 exact envelope 尺寸用于 XYZ 标注；归一化容纳源包络和实际离散顶点的并集，允许既有 MeshCodec 离散公差。此处尺寸是外包络，不是制造标注。
- 既有 GPU 清理模式保留；OpenGL 回退时立即销毁仍借用 SceneData 的旧控件，避免 `deleteLater` 留到外层 PImpl 已销毁之后。无新增 Part 裸指针或 mesh 可变所有权。
- `tests/cad_scene_tests.cpp`：从真实 worker 输出经 CadResultParser 读入轴承/电机。真实孔背景、非盒轮廓、真实像素与独立 world 投影边界、六件 ID/显隐/隔离/标尺、错误清场和生命周期检查。原生截图用 `grabFramebuffer()`，包含真实叠加标尺。
- `CMakeLists.txt`：仅注册 `cad_scene_tests` target 并链接现有 scene_view/Qt Test；不更改版本或其他 source。CAD 真实输入是显式 `--probe-dir` 参数，未添加依赖本机 runtime 路径的 CTest。
- `src/sceneview.h` 未修改；未修改 SQL、Worker、Window、Controller、Python、API、正常数据库或主树。

测试曾位于 `newtests/cad_scene_tests.cpp`，依主管最新指令归入 `tests/`；内容保持一致，保留全部 build-cad 证据。

## 源码标识

本记录完成前，SHA256（磁盘实际字节）：

| 文件 | SHA256 |
| --- | --- |
| src/sceneview.cpp | C41EB48BC8215D6D5160A7CE8BED8AFD4858E1436FC32B8F687D829E0AE2A081 |
| tests/cad_scene_tests.cpp | 41125084EDA2B2EB6A3603BB7ADFE7DA7891B52F49B94AC8C7C1C7DED291A3D7 |

提交由主管消息接收 SHA；Git 的 CRLF 规范化可能改变磁盘 SHA，独审优先按提交实际差异核对。

## 实际构建和检查

独立 `build-cad` 使用 Qt6.11.1 MinGW64 / GCC13.1.0 / CMake3.30.5 / Ninja。首次执行前已读 `scripts/build.ps1` 和 CMake 内容，实际用下列直接命令，不运行正常应用或服务：

```powershell
$env:PATH='D:/Qt/6.11.1/mingw_64/bin;D:/Qt/Tools/mingw1310_64/bin;'+$env:PATH
& 'D:/Qt/Tools/CMake_64/bin/cmake.exe' --fresh -S . -B build-cad -G Ninja '-DCMAKE_MAKE_PROGRAM=D:/Qt/Tools/Ninja/ninja.exe' '-DCMAKE_CXX_COMPILER=D:/Qt/Tools/mingw1310_64/bin/g++.exe' '-DCMAKE_PREFIX_PATH=D:/Qt/6.11.1/mingw_64' -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
& 'D:/Qt/Tools/CMake_64/bin/cmake.exe' --build build-cad --target cad_scene_tests scene_tests r07_scene_tests --parallel 4
$env:QT_PLUGIN_PATH='D:/Qt/6.11.1/mingw_64/plugins'
& ./build-cad/bin/cad_scene_tests.exe -platform offscreen --probe-dir D:/作业/简历/工作模拟/.runtime/cad-probes/20261008-233705-646 --snapshot-dir ./build-cad/cad-offscreen
& ./build-cad/bin/scene_tests.exe -platform offscreen
& ./build-cad/bin/r07_scene_tests.exe -platform offscreen --snapshot-dir ./build-cad/r07-offscreen
& ./build-cad/bin/cad_scene_tests.exe -platform windows --probe-dir D:/作业/简历/工作模拟/.runtime/cad-probes/20261008-233705-646 --snapshot-dir ./build-cad/cad-native
& ./build-cad/bin/scene_tests.exe -platform windows --snapshot-dir ./build-cad/old-native
& ./build-cad/bin/r07_scene_tests.exe -platform windows --snapshot-dir ./build-cad/r07-native
```

首次 fresh 配置、三 targets 编译及六次测试均实际 exit0。CAD offscreen 初轮测试选取最上方 AA 边缘像素触发失败；后修改测试为模型四邻域内部、最靠近投影范围中心的点击像素，未更改产品拾取行为，offscreen/native均复验通过。

测试归入 `tests/` 后，再次 `--fresh` 配置、构建 `--target cad_scene_tests --parallel 4` 和同一 offscreen 命令均实际 exit0；该轮没有再开 native 窗口，产品源码及测试内容哈希保持不变。

原生执行前后已告知主管及 CAD UI 工程师，独占窗口检查已结束。真实输出：

```text
原生OpenGL（透视投影与深度缓冲）；MSAA请求4，绘制FBO实际samples=4
R07-01 proportional cylinder differentModelPixels=0
MSAA requested=4 contextFormat.samples=0
CAD scene verified: real through-hole/curved silhouette, six world placements, ID/highlight/hide/isolation, exact envelope ruler, zoom, invalid clear, borrowed-data lifecycle.
```

因此实测四倍的是绘制 FBO；context format 的 0 不能作为实际采样结论。软件测试另记录为软件路径，不冒充原生。

## 实际模型及装配位置证据

真实已验证 worker JSON 位置：`D:/作业/简历/工作模拟/.runtime/cad-probes/20261008-233705-646/{bearing,motor}.json`。轴承一件：position=(0,0,0)，dimensions≈(22,22,7)mm，1444 顶点/1428 三角。电机六实体位置与 dimensions（毫米）：

| ID 后缀 | position | dimensions | 原生模型蓝色像素 x,y,width,height |
| --- | --- | --- | --- |
| 0001 | 2.15,0,4.8 | 46.6,42.3,9.6 | 348,377,217,145 |
| 0002 | 0,0,20.975 | 42,42,22.75 | 351,300,213,181 |
| 0003 | 0,0,37.225 | 42.3,42.3,9.75 | 342,270,229,123 |
| 0004 | 0,0,32.0000000000001 | 5,5,60.0000000000002 | 446,215,23,243 |
| 0005 | 22,≈0,6.5 | 10,15,5 | 362,448,80,58 |
| 0006 | 21.35,≈0,6.5 | 8.7,11,1 | 374,458,60,35 |

所有 ID 为 CAD-PART-xxxx。900×700/DPR=1 默认视角下，逐件隐藏其他五件，真实像素边界与基于全装配 `position+localVertex` 的独立投影预测各边差<4像素。该验证实际检测装配位置，不仅检查顶点数量或对象存在。

已查看真实 FBO PNG：轴承贯通孔保持背景且中心拾取空 ID，外圈被选中呈橙色、XYZ/毫米标尺实际可见；电机六件位置和孔/连接器真实曲面可見。

证据目录（忽略的本地生成物，不提交）：

- `build-cad/cad-native/`：`cad-bearing-hole-selected.png`、`cad-motor-assembly.png`、`cad-motor-ruler.png`、六份 `CAD-PART-000x-isolated.png`。
- `build-cad/cad-offscreen/`：对应软件证据；`build-cad/r07-native/` 有真实 FBO/屏幕/各标尺证据。
- `build-cad/r07-offscreen/` 和 `build-cad/old-native/` 保留回归证据。

## 限制和下一步

- 尚无本改动的独立审查结论；实现者检查不能代替独审。主管应安排只读实际提交差异、R08 验收及以上证据。
- 本图形 target 手工显式依赖真实 worker JSON；未验证干净交付包内路径，也未验证 UI 导入/超时/保存/重启或 SQL 迁移。这些属于主管及相应负责人的集成验收。
- 软件 fallback 仍使用 QPainter 画家排序和三角边描画；相交几何遮挡精度及视觉低于原生深度缓冲。没有宣称硬件故障注入或内存检测工具覆盖；重建/父子销毁以实际正常原生及软件路径验证。
- 隔离保持全装配坐标与相机，薄小件可能看起来较小；这是既有 Scene 状态表达，未自行新增相机定位架构。
- 标尺来源是 CAD 外包络，曲面离散有限精度；不承诺制造公差或参数识别。未扩展团队 API CAD 共享。

源码及图片完整保留，本地检查点后释放本树写入与 UI 焦点；不推送、不打 tag、不改 main。
