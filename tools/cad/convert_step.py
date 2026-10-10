"""只读STEP→真实OCCT网格协议v1；由Qt独立进程负责120秒/Windows Job约束。"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import time
import uuid
from fractions import Fraction

sys.dont_write_bytecode = True
MAX_INPUT = 100 * 1024 * 1024
MAX_SOLIDS = 1000
MAX_VERTICES = 600000
MAX_TRIANGLES = 400000
MAX_OUTPUT = 64 * 1024 * 1024
MAX_SECONDS = 120


class ConversionError(Exception):
    """安全业务失败；不输出内核异常、输入正文或堆栈。"""
    def __init__(self, code):
        self.code = code


def check_deadline(started):
    if time.monotonic() - started > MAX_SECONDS:
        raise ConversionError("TIME_LIMIT")


def hash_source(filename):
    digest = hashlib.sha256()
    size = 0
    with filename.open("rb") as source:
        while chunk := source.read(256 * 1024):
            size += len(chunk)
            if size > MAX_INPUT:
                raise ConversionError("INPUT_LIMIT")
            digest.update(chunk)
    if size == 0:
        raise ConversionError("INVALID_INPUT")
    return digest.hexdigest()


def finite(values):
    if not all(math.isfinite(value) for value in values):
        raise ConversionError("NONFINITE_GEOMETRY")
    return values


def zero_area_triangle(a, b, c):
    """仅排除输出double坐标上严格零面积；不以几何epsilon删小曲面。"""
    if a == b or b == c or c == a:
        return True
    # 与正式MeshGeometryCodec::validate逐操作相同，确保剩余输出可通过严格double面积检查。
    scale = max(abs(value) for point in (a, b, c) for value in point)
    if scale == 0:
        return True
    u = [b[i] / scale - a[i] / scale for i in range(3)]
    v = [c[i] / scale - a[i] / scale for i in range(3)]
    cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    if any(value != 0 for value in cross):
        return False
    # 极端范围可能让double叉乘下溢为零；精确有理运算区分真零和丢失精度，后者拒绝而非删除。
    exact_a, exact_b, exact_c = [[Fraction.from_float(value) for value in point] for point in (a, b, c)]
    exact_u = [exact_b[i] - exact_a[i] for i in range(3)]
    exact_v = [exact_c[i] - exact_a[i] for i in range(3)]
    exact_cross = (exact_u[1] * exact_v[2] - exact_u[2] * exact_v[1],
                   exact_u[2] * exact_v[0] - exact_u[0] * exact_v[2],
                   exact_u[0] * exact_v[1] - exact_u[1] * exact_v[0])
    if any(value != 0 for value in exact_cross):
        raise ConversionError("TRIANGLE_PRECISION")
    return True


def load_kernel(modules):
    """只加入调用者显式指定的私有目录，绝不从STEP所在目录或PYTHONPATH找模块。"""
    if not modules.is_absolute() or not modules.is_dir() or not (modules / "OCP" / "__init__.py").is_file():
        raise ConversionError("MODULES_UNAVAILABLE")
    sys.path.insert(0, str(modules.resolve()))
    try:
        import OCP
        from OCP.BRep import BRep_Tool
        from OCP.BRepBndLib import BRepBndLib
        from OCP.BRepCheck import BRepCheck_Analyzer
        from OCP.BRepMesh import BRepMesh_IncrementalMesh
        from OCP.Bnd import Bnd_Box
        from OCP.IFSelect import IFSelect_RetDone
        from OCP.STEPCAFControl import STEPCAFControl_Reader
        from OCP.TCollection import TCollection_AsciiString, TCollection_ExtendedString
        from OCP.TDataStd import TDataStd_Name
        from OCP.TDF import TDF_Label, TDF_LabelSequence, TDF_Tool
        from OCP.TDocStd import TDocStd_Document
        from OCP.TopAbs import TopAbs_COMPOUND, TopAbs_COMPSOLID, TopAbs_FACE, TopAbs_REVERSED, TopAbs_SHELL, TopAbs_SOLID, TopAbs_WIRE
        from OCP.TopExp import TopExp, TopExp_Explorer
        from OCP.TopLoc import TopLoc_Location
        from OCP.TopoDS import TopoDS, TopoDS_Iterator
        from OCP.TopTools import TopTools_IndexedMapOfShape
        from OCP.XCAFDoc import XCAFDoc_DocumentTool, XCAFDoc_ShapeTool
    except Exception as error:
        raise ConversionError("MODULES_UNAVAILABLE") from error
    if OCP.__version__ != "7.9.3.1":
        raise ConversionError("MODULES_VERSION")
    return locals()


def label_name(label, kernel):
    attribute = kernel["TDataStd_Name"]()
    if label.FindAttribute(kernel["TDataStd_Name"].GetID_s(), attribute):
        value = attribute.Get().ToExtString()
        if value.strip():
            value.encode("utf8", errors="strict")
            return value
    return None


def label_entry(label, kernel):
    value = kernel["TCollection_AsciiString"]()
    kernel["TDF_Tool"].Entry_s(label, value)
    return value.ToCString()


def leaf_solids(shape, kernel, started):
    """只递归实体容器，实体内部face/wire合法；游离面/线等不能在混合文件中静默丢弃。"""
    stack = [shape]
    solids = []
    extra = []
    while stack:
        check_deadline(started)
        current = stack.pop()
        kind = current.ShapeType()
        if kind == kernel["TopAbs_SOLID"]:
            solids.append(current)
            if len(solids) > MAX_SOLIDS:
                raise ConversionError("SOLID_LIMIT")
        elif kind in (kernel["TopAbs_COMPOUND"], kernel["TopAbs_COMPSOLID"]):
            iterator = kernel["TopoDS_Iterator"](current, True, True)
            children = []
            while iterator.More():
                children.append(iterator.Value())
                iterator.Next()
            if not children:
                extra.append(current)
            stack.extend(reversed(children))
        else:
            extra.append(current)
    return solids, extra


def covered_by_solid(shape, covered, kernel):
    """只承认拓扑+全局location真实归属；不能按相同包围盒猜测游离面的身份。"""
    stack = [shape]
    while stack:
        current = stack.pop()
        if covered.Contains(current):
            continue
        if current.ShapeType() not in (kernel["TopAbs_SHELL"], kernel["TopAbs_WIRE"]):
            return False
        iterator = kernel["TopoDS_Iterator"](current, True, True)
        children = []
        while iterator.More():
            children.append(iterator.Value())
            iterator.Next()
        if not children:
            return False
        stack.extend(children)
    return True


def read_solids(filename, kernel, started):
    """XCAF显式累计装配reference位置，叶实体再乘自己的location，源名称可重复。"""
    reader = kernel["STEPCAFControl_Reader"]()
    reader.SetNameMode(True)
    reader.SetColorMode(False)
    reader.SetLayerMode(False)
    kernel["XCAFDoc_ShapeTool"].SetAutoNaming_s(False)
    if reader.ReadFile(str(filename)) != kernel["IFSelect_RetDone"]:
        raise ConversionError("READ_FAILED")
    # OCCT只有已加载model才能设置系统单位，必须在ReadFile后/Transfer前执行。
    reader.Reader().SetSystemLengthUnit(1.0)
    document = kernel["TDocStd_Document"](kernel["TCollection_ExtendedString"]("BinXCAF"))
    if not reader.Transfer(document):
        raise ConversionError("TRANSFER_FAILED")
    shape_tool = kernel["XCAFDoc_DocumentTool"].ShapeTool_s(document.Main())
    free = kernel["TDF_LabelSequence"]()
    shape_tool.GetFreeShapes(free)
    stack = [(free.Value(index), kernel["TopLoc_Location"](), None, frozenset())
             for index in range(free.Length(), 0, -1)]
    solids = []
    unsupported_geometry = False
    extra_geometry = []
    while stack:
        check_deadline(started)
        label, placement, occurrence_name, ancestors = stack.pop()
        entry = label_entry(label, kernel)
        if entry in ancestors:
            raise ConversionError("ASSEMBLY_CYCLE")
        path = ancestors | {entry}
        if shape_tool.IsReference_s(label):
            target = kernel["TDF_Label"]()
            if not shape_tool.GetReferredShape_s(label, target):
                raise ConversionError("INVALID_ASSEMBLY")
            location = placement.Multiplied(shape_tool.GetLocation_s(label))
            stack.append((target, location, label_name(label, kernel) or occurrence_name, path))
        elif shape_tool.IsAssembly_s(label):
            shape = shape_tool.GetShape_s(label)
            if shape.IsNull():
                raise ConversionError("INVALID_ASSEMBLY")
            location = placement.Multiplied(shape.Location())
            components = kernel["TDF_LabelSequence"]()
            if not shape_tool.GetComponents_s(label, components, False):
                raise ConversionError("INVALID_ASSEMBLY")
            for index in range(components.Length(), 0, -1):
                stack.append((components.Value(index), location, None, path))
        else:
            shape = shape_tool.GetShape_s(label)
            if shape.IsNull():
                unsupported_geometry = True
                continue
            name = occurrence_name or label_name(label, kernel)
            leaf, extra = leaf_solids(shape, kernel, started)
            extra_geometry.extend(item.Located(placement.Multiplied(item.Location())) for item in extra)
            for solid in leaf:
                located = solid.Located(placement.Multiplied(solid.Location()))
                if not kernel["BRepCheck_Analyzer"](located).IsValid():
                    raise ConversionError("INVALID_SOLID")
                solids.append((located, name))
                if len(solids) > MAX_SOLIDS:
                    raise ConversionError("SOLID_LIMIT")
    if not solids:
        raise ConversionError("NO_SOLIDS")
    covered = kernel["TopTools_IndexedMapOfShape"]()
    for solid, _name in solids:
        kernel["TopExp"].MapShapes_s(solid, covered, True, True)
    unsupported_geometry = unsupported_geometry or any(not covered_by_solid(item, covered, kernel) for item in extra_geometry)
    if unsupported_geometry:
        raise ConversionError("UNSUPPORTED_GEOMETRY")
    return solids


def triangulate(solid, name, number, kernel, counts, started):
    """解析外包络定位，真实面网格转中心局部毫米坐标，合计计数失败不发布。"""
    box = kernel["Bnd_Box"]()
    kernel["BRepBndLib"].AddOptimal_s(solid, box, False, False)
    if box.IsVoid() or box.IsOpen():
        raise ConversionError("UNBOUNDED_GEOMETRY")
    bounds = finite(tuple(box.Get()))
    dimensions = finite([bounds[index + 3] - bounds[index] for index in range(3)])
    if any(value <= 0 for value in dimensions):
        raise ConversionError("INVALID_BOUNDS")
    position = finite([(bounds[index] + bounds[index + 3]) / 2.0 for index in range(3)])
    # 固定可解释精度，不以包围盒替代真实曲面；小件至少0.02mm，大件最多0.25mm。
    deflection = max(0.02, min(max(dimensions) * 0.002, 0.25))
    mesh = kernel["BRepMesh_IncrementalMesh"](solid, deflection, False, 0.25, False)
    if not mesh.IsDone():
        raise ConversionError("MESH_FAILED")
    vertices = []
    triangles = []
    explorer = kernel["TopExp_Explorer"](solid, kernel["TopAbs_FACE"])
    while explorer.More():
        check_deadline(started)
        face = kernel["TopoDS"].Face_s(explorer.Current())
        location = kernel["TopLoc_Location"]()
        triangulation = kernel["BRep_Tool"].Triangulation_s(face, location)
        if triangulation is None or triangulation.NbNodes() < 3 or triangulation.NbTriangles() < 1:
            raise ConversionError("MESH_FAILED")
        nodes = triangulation.NbNodes()
        faces = triangulation.NbTriangles()
        # 原始剖分（三角含零面积）仍计入资源预算，不能通过过滤绕过上限。
        if counts[0] + nodes > MAX_VERTICES or counts[1] + counts[2] + faces > MAX_TRIANGLES:
            raise ConversionError("MESH_LIMIT")
        transform = location.Transformation()
        offset = len(vertices)
        for index in range(1, nodes + 1):
            point = triangulation.Node(index).Transformed(transform)
            vertices.append(finite([point.X() - position[0], point.Y() - position[1], point.Z() - position[2]]))
        reversed_face = face.Orientation() == kernel["TopAbs_REVERSED"]
        # 镜像placement同样改变手性，不能只按face方向处理三角绕序。
        reverse = reversed_face != transform.IsNegative()
        kept = 0
        for index in range(1, faces + 1):
            if index % 4096 == 0:
                check_deadline(started)
            indices = list(triangulation.Triangle(index).Get())
            if len(indices) != 3 or any(type(value) is not int or value < 1 or value > nodes for value in indices):
                raise ConversionError("INVALID_TRIANGLES")
            if reverse:
                indices[1], indices[2] = indices[2], indices[1]
            local_indices = [offset + value - 1 for value in indices]
            if zero_area_triangle(*(vertices[value] for value in local_indices)):
                counts[2] += 1
                continue
            triangles.append(local_indices)
            kept += 1
        # 每个原Face至少仍有非零三角；不允许过滤变成静默丢面或丢实体。
        if kept == 0:
            raise ConversionError("DEGENERATE_FACE")
        counts[0] += nodes
        counts[1] += kept
        explorer.Next()
    if not triangles:
        raise ConversionError("MESH_FAILED")
    return {"id": "CAD-PART-%04d" % number, "name": name or "实体%04d" % number, "material": "未提供",
            "shape": "mesh", "dimensions": dimensions, "position": position,
            "mesh": {"vertices": vertices, "triangles": triangles}}


def publish_json(result, output, started=None):
    """同目录自己创建的partial，完整fsync后不覆盖发布；失败只删自己partial。"""
    temporary = output.with_name(output.name + ".partial-" + uuid.uuid4().hex)
    descriptor = None
    owned = False
    try:
        descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        owned = True
        encoder = json.JSONEncoder(ensure_ascii=False, allow_nan=False, separators=(",", ":"))
        total = 0
        with os.fdopen(descriptor, "wb") as target:
            descriptor = None
            for index, chunk in enumerate(encoder.iterencode(result)):
                if started is not None and index % 4096 == 0:
                    check_deadline(started)
                data = chunk.encode("utf8", errors="strict")
                total += len(data)
                if total > MAX_OUTPUT:
                    raise ConversionError("OUTPUT_LIMIT")
                target.write(data)
            target.flush()
            os.fsync(target.fileno())
        if started is not None:
            check_deadline(started)
        if os.name == "nt":
            os.rename(temporary, output)  # Windows rename拒绝已存在目标，包括竞态创建。
        else:
            os.link(temporary, output)  # POSIX rename会覆盖；hardlink发布完整文件且目标必须不存在。
            temporary.unlink()
        owned = False
    except FileExistsError as error:
        raise ConversionError("OUTPUT_EXISTS") from error
    finally:
        try:
            if descriptor is not None:
                os.close(descriptor)
        finally:
            if owned:
                temporary.unlink(missing_ok=True)


def convert(input_file, output_file, modules):
    """读取绝对路径STEP与受信模块，完整校验后发布协议文件，失败不覆盖已有结果。"""
    started = time.monotonic()
    if not input_file.is_absolute() or not output_file.is_absolute() or not input_file.is_file():
        raise ConversionError("INVALID_PATH")
    if not output_file.parent.is_dir():
        raise ConversionError("INVALID_OUTPUT_PATH")
    if os.path.lexists(output_file):
        raise ConversionError("OUTPUT_EXISTS")
    if input_file.suffix.lower() not in (".step", ".stp"):
        raise ConversionError("INPUT_FORMAT")
    if input_file.stat().st_size > MAX_INPUT:
        raise ConversionError("INPUT_LIMIT")
    input_file.name.encode("utf8", errors="strict")
    source_hash = hash_source(input_file)
    kernel = load_kernel(modules)
    solids = read_solids(input_file, kernel, started)
    counts = [0, 0, 0]  # vertices、输出triangles、明确审计的零面积triangles。
    parts = [triangulate(solid, name, index, kernel, counts, started)
             for index, (solid, name) in enumerate(solids, 1)]
    check_deadline(started)
    # 内核按路径再次读取，前后完整hash一致才承诺该source身份；变化时不留下可导入结果。
    if hash_source(input_file) != source_hash:
        raise ConversionError("INPUT_CHANGED")
    result = {"protocol": "equipment-cad-mesh", "version": 1, "unit": "mm", "sourceName": input_file.name,
              "sourceSha256": source_hash, "parts": parts}
    publish_json(result, output_file, started)
    return counts, len(parts)


def main():
    parser = argparse.ArgumentParser(description="STEP to equipment CAD mesh v1")
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--modules", required=True)
    args = parser.parse_args()
    try:
        # Linux保底并尊重已有限额；Windows准确2GiB/进程树终止由Qt父进程Job实施。
        if os.name == "posix":
            import resource
            soft, hard = resource.getrlimit(resource.RLIMIT_AS)
            cap = 2 * 1024 ** 3 if hard == resource.RLIM_INFINITY else min(hard, 2 * 1024 ** 3)
            soft_cap = cap if soft == resource.RLIM_INFINITY else min(soft, cap)
            resource.setrlimit(resource.RLIMIT_AS, (soft_cap, cap))
        counts, parts = convert(Path(args.input), Path(args.output), Path(args.modules))
        print("CAD_CONVERT_OK parts=%d vertices=%d triangles=%d filteredZeroArea=%d" %
              (parts, counts[0], counts[1], counts[2]), flush=True)
        return 0
    except ConversionError as error:
        print("CAD_CONVERT_FAILED " + error.code, file=sys.stderr, flush=True)
    except Exception:
        print("CAD_CONVERT_FAILED CONVERSION_FAILED", file=sys.stderr, flush=True)
    return 1


if __name__ == "__main__":
    sys.exit(main())
