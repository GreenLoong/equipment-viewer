"""校验下载固定Windows CAD运行库；不改系统Python、不运行安装脚本。"""
from pathlib import Path
import hashlib
import json
import shutil
import sys
import urllib.request
import zipfile
import argparse
import os
import stat
import subprocess
import tempfile
import uuid

sys.dont_write_bytecode = True
LICENSES = (
    ('OpenSSL-LICENSE.txt', 'https://raw.githubusercontent.com/openssl/openssl/openssl-3.5.9/LICENSE.txt', '7d5450cb2d142651b8afa315b5f238efc805dad827d91ba367d8516bc9d49e7a'),
    ('Pybind11-LICENSE.txt', 'https://raw.githubusercontent.com/pybind/pybind11/v2.13.6/LICENSE', '83965b843b98f670d3a85bd041ed4b372c8ec50d7b4a5995a83ac697ba675dcb'),
    ('RapidJSON-LICENSE.txt', 'https://raw.githubusercontent.com/Tencent/rapidjson/v1.1.0/license.txt', 'a140e5d46fe734a1c78f1a3c3ef207871dd75648be71fdda8e309b23ab8b1f32'),
    ('LibTomMath-LICENSE.txt', 'https://raw.githubusercontent.com/libtom/libtommath/v1.3.0/LICENSE', '2fa64b163659f41965c9815882a8296d3d03ff546b76153e11445f9bdecf955a'),

    ('Microsoft-VC-Runtime-LICENSE.docx', 'https://visualstudio.microsoft.com/wp-content/uploads/2021/09/Visual-C-Runtime-2015-2022-License-1.docx', 'f1e3d56ceb2ad68aae0711b910375009e651ac5530fa0760f0dea6e81e54fae1'),
    ('OCP-LICENSE.txt', 'https://raw.githubusercontent.com/CadQuery/OCP/7.9.3.1/LICENSE', 'a13caea71627202ad33cc4cafafdd18e667e16716488f8d9c568127121fb89fd'),
    ('OCCT-LGPL-2.1.txt', 'https://raw.githubusercontent.com/Open-Cascade-SAS/OCCT/V7_9_3/LICENSE_LGPL_21.txt', 'e237fa56668030e928551ddd60f05df5fe957f75eab874bbd017e085ed722e7c'),
    ('OCCT-LGPL-EXCEPTION.txt', 'https://raw.githubusercontent.com/Open-Cascade-SAS/OCCT/V7_9_3/OCCT_LGPL_EXCEPTION.txt', '04580a884ea6cea294402649ff7b5cbb167d47462d1340a4ed33e550db10a81b'),
    ('OCP-BUILD-LICENSE.txt', 'https://raw.githubusercontent.com/CadQuery/ocp-build-system/4a3cecf/LICENSE', 'c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4'),
    ('FreeImage-LICENSE.txt', 'https://freeimage.sourceforge.io/freeimage-license.txt', '37f80b2998523e0e780cbab2774e1ee7eb4b7945f1ff232f5ece22ce037b6282'),
    ('FreeType-LICENSE.txt', 'https://raw.githubusercontent.com/freetype/freetype/VER-2-12-1/docs/FTL.TXT', '08c135755dd589039470f1fdbb400daaabaaa50d0b366d19cebff4d22986baa1'),
    ('OpenEXR-LICENSE.txt', 'https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr/v3.4.4/LICENSE.md', '84853c91d4c2e0310741d4773469d98bb93e61029608e0ba0882a3fffdf330bd'),
    ('Imath-LICENSE.txt', 'https://raw.githubusercontent.com/AcademySoftwareFoundation/Imath/v3.2.2/LICENSE.md', 'c20236d3b39fd20eba8e3d1fb3b892a5483df2e7d8d61bf43f165d3fac22f601'),
    ('JPEG-LICENSE.txt', 'https://raw.githubusercontent.com/libjpeg-turbo/libjpeg-turbo/3.1.2/LICENSE.md', '2189dc45a8fe96204069f8124caa53a148dfdc193f50f584c6ca7849a6072872'),
    ('Lerc-LICENSE.txt', 'https://raw.githubusercontent.com/Esri/lerc/v4.0.0/LICENSE', '77a8b761727c75e2167b15bfdf61b2c0bcf8792271228bebe80779106ad00671'),
    ('LZMA-LICENSE.txt', 'https://raw.githubusercontent.com/tukaani-project/xz/v5.8.2/COPYING', '616a3ad264ce29b8f1cb97e53037b139d406899ca8d1f799651e17bfa09830b8'),
    ('PNG-LICENSE.txt', 'https://raw.githubusercontent.com/pnggroup/libpng/v1.6.55/LICENSE', 'bdb0a645ea18c60507d0368379b1ac5474b92255fcc2d115e07486a7672ba526'),
    ('WebP-LICENSE.txt', 'https://raw.githubusercontent.com/webmproject/libwebp/v1.6.0/COPYING', '5aec868f669e384a22372a4e8a1a6cd7d44c64cd451f960ca69cc170d1e13acf'),
    ('OpenJPEG-LICENSE.txt', 'https://raw.githubusercontent.com/uclouvain/openjpeg/v2.5.4/LICENSE', 'a6af136f3e15038a666b61f376612a07d9a4e48cb7c01adbf3e33b3f14ab49b6'),
    ('OpenJPH-LICENSE.txt', 'https://raw.githubusercontent.com/aous72/OpenJPH/0.25.0/LICENSE', '5ddf5177863dfc9ab65fa129d587db651241f00e21ed2427b218bea997591f98'),
    ('LibRaw-LICENSE.txt', 'https://raw.githubusercontent.com/LibRaw/LibRaw/0.21.4/LICENSE.LGPL', 'eea173a556abac0370461e57e12aab266894ea6be3874c2be05fd87871f75449'),
    ('TIFF-LICENSE.txt', 'https://gitlab.com/libtiff/libtiff/-/raw/v4.7.1/LICENSE.md', '0e27c2382d7b8147972bbb746e04059a1152c8d0fda9d03ef1399d1a433c4ade'),
    ('Zlib-LICENSE.txt', 'https://raw.githubusercontent.com/madler/zlib/v1.3.1/LICENSE', '845efc77857d485d91fb3e0b884aaa929368c717ae8186b66fe1ed2495753243'),
    ('Zstd-LICENSE.txt', 'https://raw.githubusercontent.com/facebook/zstd/v1.5.7/LICENSE', '7055266497633c9025b777c78eb7235af13922117480ed5c674677adc381c9d8'),
    ('Libdeflate-LICENSE.txt', 'https://raw.githubusercontent.com/ebiggers/libdeflate/v1.25/COPYING', '4ad69099cb4374836fd27583d9991e2838cd86b6e4666ab26b2d32582c91e73a'),
    ('JPEG-IJG-README.txt', 'https://raw.githubusercontent.com/libjpeg-turbo/libjpeg-turbo/3.1.2/README.ijg', '75815e3bf6484201a3c3d17a1bbf10f2e8e3237f84df10a2357ea896db2a81d6'),
    ('LZMA-0BSD.txt', 'https://raw.githubusercontent.com/tukaani-project/xz/v5.8.2/COPYING.0BSD', '0b01625d853911cd0e2e088dcfb743261034a091bb379246cb25a14cc4c74bf1'),
    ('WebP-PATENTS.txt', 'https://raw.githubusercontent.com/webmproject/libwebp/v1.6.0/PATENTS', 'cc3273e0694ea5896145e0677699b53471b03ea43021ddc50e7923fbb9f5023c'),
    ('LibRaw-COPYRIGHT.txt', 'https://raw.githubusercontent.com/LibRaw/LibRaw/0.21.4/COPYRIGHT', 'd6ed03c89e25fcda5db454c1badc2c321f241799aab2062418a7fb3742c935ce'),
    ('LCMS-LICENSE.txt', 'https://raw.githubusercontent.com/mm2/Little-CMS/lcms2.18/LICENSE', '6dbd60437f8ef91d8de1f08ad75882547fd4931bfcc3566a0735f28db1484d31'),
)

ARTIFACTS = (
    ("python-3.14.8-embed-amd64.zip", "https://www.python.org/ftp/python/3.14.8/python-3.14.8-embed-amd64.zip", "a93abe456ab01bd96d7a085b3cdb6566b3063f4241360d114142fbdb07f0a310"),
    ("cadquery_ocp_novtk-7.9.3.1-cp314-cp314-win_amd64.whl", "https://files.pythonhosted.org/packages/95/05/2aebac5faa4e7f3a7e9be55dd178f385e88ae05a19b0abc1de78610658fb/cadquery_ocp_novtk-7.9.3.1-cp314-cp314-win_amd64.whl", "e5ed4caf7c2982ebd8175b35358d2b5fa1a1cf7557cb7a94e87d6a5a0f1e697e"),
    ("cadquery_ocp_proxy-7.9.3.1-py3-none-any.whl", "https://files.pythonhosted.org/packages/a6/84/566162a2fe2c6f5519bf6c9f8ca9535173cee42cc6d547d02c27b28c3402/cadquery_ocp_proxy-7.9.3.1-py3-none-any.whl", "8259b53668784b682fe2e4a6c1fed8293886d52bf6c3b2ecc2aec986c49e92d7"),
)


def digest(file):
    """流式摘要，避免将大型轮包整体加载内存。"""
    result = hashlib.sha256()
    with file.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def extract_checked(archive, target):
    """在新暂存目录内检查路径、软链接和解压预算，再提取。"""
    root = target.resolve()
    with zipfile.ZipFile(archive) as source:
        if sum(item.file_size for item in source.infolist()) > 512 * 1024 * 1024:
            raise RuntimeError("运行库展开超出预算")
        seen = set()
        for item in source.infolist():
            path = (root / item.filename).resolve()
            key = item.filename.rstrip('/').lower()
            if (key in seen or '\\' in item.filename or ':' in item.filename
                    or '..' in Path(item.filename).parts or not path.is_relative_to(root)
                    or (item.external_attr >> 16) & 0o170000 == 0o120000):
                raise RuntimeError("运行库归档路径不安全")
            seen.add(key)
        source.extractall(root)


def ordinary(path):
    """拒绝路径及祖先中的junction/软链接，不对跨出目标的目录复制或清理。"""
    path = Path(os.path.abspath(path))
    for item in (path, *path.parents):
        if item.exists() or item.is_symlink():
            info = item.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
                raise RuntimeError('不接受重解析路径：' + str(item))
    return path


def obtain(downloads, artifact, project, offline):
    """固定URL和SHA，复用已核缓存；partial独立命名，失败保留，不覆盖旧下载。"""
    name, url, expected = artifact
    target = ordinary(downloads / name)
    if not target.exists():
        for base in (project / '.runtime/downloads/python314', project / '.runtime/downloads/cad',
                     project / '.runtime/license-probes'):
            seed = ordinary(base / name)
            if seed.is_file() and digest(seed) == expected:
                shutil.copyfile(seed, target)
                break
        else:
            if offline:
                raise RuntimeError('离线缺少已核缓存：' + name)
            temporary = downloads / (name + '.partial-' + uuid.uuid4().hex)
            # Microsoft许可直链拒绝urllib默认UA；固定正文仍须通过SHA，不接受网页/重定向错误内容。
            request = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(request, timeout=30) as response, temporary.open('xb') as output:
                total = 0
                while block := response.read(1024 * 1024):
                    total += len(block)
                    if total > (128 * 1024 * 1024 if name.endswith(('.zip', '.whl')) else 512 * 1024):
                        raise RuntimeError('下载超出预算，partial保留：' + name)
                    output.write(block)
            if digest(temporary) != expected:
                raise RuntimeError('下载摘要不匹配，partial保留：' + name)
            temporary.rename(target)
    if digest(target) != expected:
        raise RuntimeError('缓存摘要不匹配，文件保留：' + name)
    return target


def inventory(archives):
    """从固定归档生成逐文件SHA，不仅凭Python签名或版本号信任整个目录。"""
    result = {}
    total = 0
    for index, archive in enumerate(archives):
        with zipfile.ZipFile(archive) as source:
            for item in source.infolist():
                if item.is_dir():
                    continue
                total += item.file_size
                if total > 512 * 1024 * 1024:
                    raise RuntimeError('运行库合计展开超出预算')
                name = ('site-packages/' if index else '') + item.filename
                if '\\' in name or ':' in name or name.startswith('/') or '..' in Path(name).parts or name in result:
                    raise RuntimeError('归档路径不安全或冲突')
                checksum = hashlib.sha256()
                with source.open(item) as handle:
                    for block in iter(lambda: handle.read(1024 * 1024), b''):
                        checksum.update(block)
                result[name] = checksum.hexdigest()
    return result


def verify(runtime, payload, project):
    """现有无marker运行库也逐文件比对；未知文件拒绝且保留，不覆盖用户内容。"""
    runtime = ordinary(runtime)
    if not runtime.is_dir():
        raise RuntimeError('运行库不存在：' + str(runtime))
    for name, expected in payload.items():
        file = ordinary(runtime / name)
        if not file.is_file() or digest(file) != expected:
            raise RuntimeError('运行库文件不匹配：' + name)
    documents = {'third-party-licenses/' + name: sha for name, _url, sha in LICENSES}
    documents.update({'convert_step.py': digest(project / 'tools/cad/convert_step.py'),
                      'setup_runtime.py': digest(Path(__file__))})
    marker = runtime / 'runtime-manifest.json'
    owned = {}
    if marker.exists():
        if marker.stat().st_size > 1024 * 1024:
            raise RuntimeError('归属清单过大')
        data = json.loads(marker.read_text(encoding='utf8'))
        if data.get('purpose') != 'equipment-viewer-cad-runtime' or data.get('format') != 2:
            raise RuntimeError('未知运行库归属清单')
        owned = data.get('files', {})
    for file in runtime.rglob('*'):
        ordinary(file)
        if not file.is_file():
            continue
        name = file.relative_to(runtime).as_posix()
        if name in payload:
            continue
        # 禁写字节码不等于禁读；未知缓存可覆盖已核源码，必须拒绝并保留原目录。
        if name == 'runtime-manifest.json':
            continue
        if name not in documents and name != 'NOTICE.txt':
            raise RuntimeError('运行库含未知文件，保留：' + name)
        if digest(file) != documents.get(name) and digest(file) != owned.get(name):
            raise RuntimeError('运行库文档/源码不匹配，保留：' + name)


def probe(runtime):
    """验证payload后才执行私有Python；不读取系统site或来源目录的旧pyc。"""
    with tempfile.TemporaryDirectory(prefix='equipment-cad-probe-') as temp:
        code = ('import sys,json;sys.pycache_prefix=sys.argv[2];sys.path.insert(0,sys.argv[1]);'
                'import OCP;from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox;'
                'assert not BRepPrimAPI_MakeBox(1,2,3).Shape().IsNull();'
                'print(json.dumps([sys.version.split()[0],OCP.__version__]))')
        env = {key: os.environ[key] for key in ('SystemRoot', 'WINDIR') if key in os.environ}
        env.update({'TEMP': temp, 'TMP': temp, 'PATH': str(Path(env.get('SystemRoot', 'C:/Windows')) / 'System32')})
        run = subprocess.run([str(runtime / 'python.exe'), '-I', '-B', '-c', code,
                              str(runtime / 'site-packages'), temp], env=env, cwd=temp,
                             capture_output=True, timeout=30, check=True)
        if len(run.stdout) > 16384 or len(run.stderr) > 16384 or json.loads(run.stdout) != ['3.14.8', '7.9.3.1']:
            raise RuntimeError('私有运行库probe版本/输出无效')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', nargs='?', choices=('prepare', 'verify', 'sync'), default='prepare')
    parser.add_argument('--runtime', type=Path)
    parser.add_argument('--destination', type=Path)
    parser.add_argument('--offline', action='store_true')
    args = parser.parse_args()
    if sys.platform != "win32":
        raise RuntimeError("此固定运行库面向Windows x64")
    project = Path(__file__).resolve().parents[2]
    if not (project / 'tools/cad/convert_step.py').is_file():
        raise RuntimeError('包内setup副本供审阅；请在工程tools/cad入口准备运行库，未写入其他目录')
    downloads = ordinary(project / ".runtime" / "downloads" / "cad-bootstrap")
    downloads.mkdir(parents=True, exist_ok=True)
    runtime = ordinary(args.runtime or project / '.runtime/cad-distribution/cad-runtime')
    if not LICENSES:
        raise RuntimeError('固定许可清单尚未冻结')
    files = [obtain(downloads, item, project, args.offline) for item in ARTIFACTS]
    payload = inventory(files)
    if args.mode in ('verify', 'sync') or (runtime.exists() and not args.destination):
        verify(runtime, payload, project)
        probe(runtime)
        print('CAD_RUNTIME_VERIFIED: ' + str(runtime))
        if args.mode != 'sync':
            return
    destination = ordinary(args.destination or runtime)
    if destination.name.lower() != 'cad-runtime' or (args.mode == 'sync' and
            (destination == runtime or runtime in destination.parents or destination in runtime.parents)):
        raise RuntimeError('目标须为独立cad-runtime目录，不等于/嵌套/包含源目录')
    if destination.exists():
        verify(destination, payload, project)
    licenses = [obtain(downloads, item, project, args.offline) for item in LICENSES]
    # 暂存目录不覆盖用户已有runtime，失败保留；成功后一次发布。
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = destination.with_name('.cad-runtime-stage-' + uuid.uuid4().hex)
    staging.mkdir()
    extract_checked(files[0], staging)
    modules = staging / "site-packages"
    modules.mkdir()
    for wheel in files[1:]:
        extract_checked(wheel, modules)
    shutil.copyfile(project / "tools" / "cad" / "convert_step.py", staging / "convert_step.py")
    shutil.copyfile(Path(__file__), staging / 'setup_runtime.py')
    license_dir = staging / 'third-party-licenses'
    license_dir.mkdir()
    for file in licenses:
        shutil.copyfile(file, license_dir / file.name)
    notice = ('Equipment Viewer CAD runtime notices\n\nPython原始LICENSE.txt保留；OCP/构建系统Apache-2.0，OCCT LGPL-2.1+exception。\n'
              '许可参考tag不代表未读取版本资源的DLL实际版本。本项目未修改上游DLL；上游构建可能有补丁。\n'
              '源码获取：CPython https://github.com/python/cpython/tree/v3.14.8\n'
              'OCP https://github.com/CadQuery/OCP/tree/7.9.3.1\nOCCT https://github.com/Open-Cascade-SAS/OCCT/tree/V7_9_3\n'
              '轮包构建配方/补丁与proxy源码 https://github.com/CadQuery/ocp-build-system/tree/4a3cecf\n'
              '该配方Windows依赖FreeType2.12.*、FreeImage3.18.*；其间接DLL清单附后。\n'
              'DLL实际版本仅已读文件资源：FreeType2.12.1/JPEG3.1.2/LCMS2.18.0/LZMA5.8.2/TIFF4.7.1/zlib1.3.1/zstd1.5.7。\n'
              'Python附带OpenSSL3.5.9、SQLite3.50.4、VC Runtime14.51.36247；wheel另附VC14.44.35208。\nMicrosoft文档为官方2015-2022条款参考，不虚称已证明新工具链全部许可对应。\n其余版本未证明，不虚构完整对应源码或宣称本NOTICE替代上游许可。\n'
              '各组件许可/源码参考URL如下。自有转换器/准备脚本源码随包提供。\n\n'
              + '\n'.join(name + ': ' + url for name, url, _sha in LICENSES)
              + '\n\n固定轮包DLL/PYD：\n' + '\n'.join(sorted(name for name in payload if name.lower().endswith(('.dll', '.pyd')))) + '\n')
    (staging / 'NOTICE.txt').write_text(notice, encoding='utf8')
    owned = {file.relative_to(staging).as_posix(): digest(file) for file in staging.rglob('*') if file.is_file()}
    (staging / "runtime-manifest.json").write_text(json.dumps({
        "purpose": "equipment-viewer-cad-runtime", "format": 2, "python": "3.14.8", "ocp": "7.9.3.1",
        "artifacts": [{"name": name, "url": url, "sha256": sha} for name, url, sha in ARTIFACTS],
        "licenses": [{"name": name, "url": url, "sha256": sha} for name, url, sha in LICENSES], "files": owned
    }, indent=2), encoding="utf8")
    verify(staging, payload, project)
    probe(staging)
    if destination.exists():
        # 仅添加已核缺失文件；已存在且不同的内容提前拒绝，不做Force覆盖或删除。
        staged_files = [file for file in staging.rglob('*') if file.is_file()]
        for file in staged_files:
            target = destination / file.relative_to(staging)
            if target.exists() and digest(target) != digest(file):
                raise RuntimeError('目标已有不同内容，保留目标及stage：' + str(target))
        for file in staged_files:
            target = destination / file.relative_to(staging)
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                with target.open('xb') as output, file.open('rb') as source:
                    shutil.copyfileobj(source, output)
        # 只清理本次刚创建的绝对stage；现有runtime一项也不删除。
        if ordinary(staging).parent != destination.parent or not staging.name.startswith('.cad-runtime-stage-'):
            raise RuntimeError('暂存目录归属异常，保留')
        shutil.rmtree(staging)
    else:
        staging.rename(destination)
    print('CAD_RUNTIME_READY: ' + str(destination))


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print("CAD运行库准备失败：" + str(error), file=sys.stderr)
        sys.exit(1)
