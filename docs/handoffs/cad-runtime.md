# CAD私有运行库准备交接

负责人cad_ui_r08，主树仅tools/cad/setup_runtime.py、scripts/sync-cad-runtime.ps1及本文；不提交Git，不修改业务/CMake/build/package，不删除或覆盖现有.runtime/cad-runtime。

当前已读两脚本全稿、主AGENTS及有效规范。现有3.14.8 embed与OCP novtk/proxy7.9.3.1，三个固定下载SHA已核；源runtime未带runtime-manifest。现bootstrap仅AST未执行，已存在runtime直接拒绝；现sync直接Force复制，会覆盖混杂目标，待改为来源核验/独立暂存/按归属清单安全发布与重复同步。

已核DLL清单：wheel除OCCT TK*还附FreeImage、FreeType、OpenEXR/Iex/IlmThread、Imath、JPEG、LCMS、Lerc、LZMA、PNG、WebP/SharpYUV、OpenJPEG/OpenJPH、LibRaw、TIFF、zlib、zstd/libdeflate和Microsoft运行库。现只有OCP Apache2、OCCT LGPL2.1+exception及Python LICENSE；不能写“全部DLL许可已覆盖”。已核官方CadQuery/OCP7.9.3.1 LICENSE及Open-Cascade-SAS/OCCT V7_9_3两许可正文地址，现文件SHA分别A13CAEA7…89FD、E237FA56…2E7C、04580A88…A81B。扩展许可与明确来源/版本边界进行中，不编造对应二进制版本。

review_v1已通知同一文件独占和最终哈希复审；没有提交主树Git。源码已经完整读取并实际运行。

## 最终冻结

- setup_runtime.py SHA256：6F89289CAFA600FE3D400C0C7F7B47E5B9B61BA29135BE455AE0CD774A955B92。
- sync-cad-runtime.ps1 SHA256：66D94479BBBEF46FDD08CDD983B9D5AD59EF266944464AE146C05CA696D34D1D；UTF8 BOM保留，PowerShell5.1可读取。
- canonical：D:/作业/简历/工作模拟/.runtime/cad-distribution/cad-runtime。旧手工.runtime/cad-runtime含pip改RECORD与INSTALLER/REQUESTED/direct_url/pyc，严格wheel核验拒绝，未删除/放松其完整性要求。
- 自己新建canonical在最后正文补足前的快照已保存到.runtime/cad-distribution/pre-freeze-20261009-054415/cad-runtime，没有覆盖旧runtime。root另将旧build/bin runtime保存在.recovery/runtime/20261009-053847/cad-runtime，这项由主管操作。

## 准备及同步入口

在工程根目录，用明确受信的Python运行：`C:/Users/23871/AppData/Local/Programs/Python/Python312/python.exe -I tools/cad/setup_runtime.py prepare`。首次无目录时下载固定官方zip/wheels及许可正文，逐SHA验证、在新stage解包、完整payload比对、隔离probe成功后rename到新canonical；不安装系统Python/不运行pip或安装脚本。已有canonical的默认prepare仅verify/probe后返回，不宣称更新其文档或覆盖旧源码。已有不同版本/未知目录应保留，另指定新独立`--destination .../cad-runtime`。

已核缓存可加`--offline`。固定缓存.runtime/downloads/cad-bootstrap；复用旧已核download/python314、download/cad及本次许可探针只作为摘要匹配种子，不因同名即信任。下载partial使用UUID，错误保留。

`scripts/sync-cad-runtime.ps1 -Destination <独立绝对cad-runtime路径> [-Offline]` 默认用canonical私有exe；先核PSF签名和官方embed所有根文件，再由setup核完整wheel及目录内容，不能只凭exe签名信任DLL。也可显式`-BootstrapPython <受信绝对Python路径>`，不从PATH搜索。目标不得与源重叠或为重解析路径；新目标stage成功后发布，已有合法同内容目标只补缺失文件，不Force覆盖/删除；未知或不同内容保持原样。未来源码变化同步到旧目标可能因不同文件而拒绝，应保留旧目录并用新独立输出。

## 实际验证及许可边界

最终prepare --offline实际exit0；canonical中30份固定官方许可正文/参考文件、Python原始LICENSE.txt、NOTICE、worker/bootstrap源码、制品与逐文件SHA清单。默认wrapper不借系统Python，fresh sync到.runtime/runtime-final-check-20261009-054426/cad-runtime实际exit0；此前稿已测重复同步与未知目标keep.txt内容保存，最终稿同边界复验结果随后追加。签名PythonSoftwareFoundation Valid；隔离-I/-B probe实际Python3.14.8/OCP7.9.3.1并创建非空内核box，仅用于ABI验证，不把box作为用户CAD模型。

官方来源固定在源码LICENSES/ARTIFACTS及包内manifest，全部保存正文SHA。额外包括OpenSSL、Pybind11、RapidJSON、LibTomMath及JPEG IJG、WebP PATENTS等必要版权文本；Microsoft官方DOCX直链默认urllib UA被403，采用正常UA后按固定SHA实际网络获取成功f1e3d56c…54fae1。Python原LICENSE保留完整，第三方正文不是人工编造。NOTICE列出actual DLL/PYD及已读PE版本，未查明的DLL版本不编造；许可证参考tag不声称已证明全部二进制对应源码，给上游OCP/OCCT/CPython/固定构建配方4a3cecf与补丁获取链接。尤其Python VC14.51与wheel VC14.44并存，官方2015-2022 Microsoft条款作为参考，不写全新工具链法律对应已证明。

Qt self-delete诊断独立处理：只补原UI树test+handoff提交c5947e0，业务未改；一次明确主build runtime的诊断test运行exit0未复现，不把timeout误述为回调未投递。原失败log保留；主管/独审后续复核已单列，不借本运行库工作抹去。

最终稿边界复验：同.runtime/runtime-final-check-20261009-054426/cad-runtime重复sync exit0，所有文件SHA前后完全相同（FINAL_REPEAT_CONTENT_UNCHANGED=1）；unknown/cad-runtime只有受控keep.txt，sync按预期拒绝，内容SHA及文件数保持（FINAL_UNKNOWN_TARGET_REJECTED/PRESERVED=1）。主legacy/source/data未修改。两脚本哈希保持上述冻结值，待独审最终确认，由主管打包与提交。

## 独审R08-R01后的集成修正

审查者实证：原6F892校验允许未核__pycache__/*.pyc，禁写字节码并不禁止读取，unchecked-hash缓存可替代已核源码执行。主管接手最小修：只有官方payload路径直接放行，非payload缓存走未知文件拒绝并保留原目录；worker88735和sync66D9未改。新setup SHA0ABD3A067E1250B7014984C7ACBF090544E24707398C897686413FD013EB9BA6，待独立同一恶意缓存fixture复核，旧6F892结论不能沿用。

主build旧6F892目录完整保存在.recovery/runtime/20261009-055427/cad-runtime（路径核在工程内且非reparse）；新default同步0ABD脚本到空build/bin/cad-runtime已退出0，source inventory/probe通过。原canonical附带的旧源码仅作为有manifest的历史文件保留，sync使用工程当前脚本在新stage复制更新源码；不同已存在目标继续拒绝，不强制覆盖。无数据库操作或扫描停止未知进程。
