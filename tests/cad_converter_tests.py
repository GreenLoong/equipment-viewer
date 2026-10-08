"""独立真实OCCT/CLI验证；公开机械样本与受控异常/单位夹具分开记录。"""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
PROJECT = Path(__file__).resolve().parents[1]
CONVERTER = PROJECT / "tools" / "cad" / "convert_step.py"
MODULES = PROJECT / ".runtime" / "cad-python" / "site-packages"
SAMPLES = PROJECT / ".runtime" / "cad-samples"
CLOSED_ASSEMBLY = None


def load_converter():
    specification = importlib.util.spec_from_file_location("converter_under_test", CONVERTER)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def signed_volume(mesh):
    terms = []
    vertices = mesh["vertices"]
    for ia, ib, ic in mesh["triangles"]:
        a, b, c = vertices[ia], vertices[ib], vertices[ic]
        terms.append(a[0] * (b[1] * c[2] - b[2] * c[1])
                     - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0]))
    return math.fsum(terms) / 6.0


class ConverterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = load_converter()
        cls.private_root = (PROJECT / ".runtime" / "cad-tests").resolve()
        cls.private_root.mkdir(parents=True, exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="独立CAD-", dir=cls.private_root)
        cls.root = Path(cls.temporary.name).resolve()
        if not cls.root.is_relative_to(cls.private_root):
            raise RuntimeError("Unsafe temporary workspace")
        cls.addClassCleanup(cls.temporary.cleanup)  # 仅递归清本次已核绝对路径的独立临时目录。
        cls.bearing_source = cls.root / "真实 轴承.step"
        cls.motor_source = cls.root / "闭合 装配.step"
        cls.original_motor_source = cls.root / "原NEMA混合开放几何.step"
        shutil.copyfile(SAMPLES / "608ZZ_Ball_Bearing.step", cls.bearing_source)
        shutil.copyfile(SAMPLES / "NEMA_17_with_connector.step", cls.original_motor_source)
        cls.kernel = cls.module.load_kernel(MODULES)
        if CLOSED_ASSEMBLY:
            shutil.copyfile(CLOSED_ASSEMBLY, cls.motor_source)
        else:
            # 明确受控闭合几何夹具，不称为公开机械模型；真实替代样例由主管选择后--assembly给定。
            from OCP.BRep import BRep_Builder
            from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox
            from OCP.TopoDS import TopoDS_Compound
            from OCP.gp import gp_Pnt
            from OCP.STEPControl import STEPControl_Writer, STEPControl_AsIs
            shape = TopoDS_Compound(); builder = BRep_Builder(); builder.MakeCompound(shape)
            for index in range(6):
                builder.Add(shape, BRepPrimAPI_MakeBox(gp_Pnt(10 * index, 3 * index, 5 * index), 2 + index, 3 + index, 4 + index).Shape())
            writer = STEPControl_Writer(); writer.Transfer(shape, STEPControl_AsIs); writer.Write(str(cls.motor_source))
        cls.bearing_file = cls.root / "bearing.json"
        cls.motor_file = cls.root / "motor.json"
        cls.run_conversion(cls.bearing_source, cls.bearing_file)
        cls.run_conversion(cls.motor_source, cls.motor_file)
        cls.bearing = json.loads(cls.bearing_file.read_text(encoding="utf8"))
        cls.motor = json.loads(cls.motor_file.read_text(encoding="utf8"))

    @classmethod
    def run_conversion(cls, source, output, expected=0, modules=None, cwd=None):
        command = [sys.executable, "-I", str(CONVERTER), "--input", str(source), "--output", str(output),
                   "--modules", str(modules if modules is not None else MODULES)]
        result = subprocess.run(command, capture_output=True, text=True, encoding="utf8", errors="replace", timeout=125, cwd=cwd)
        if result.returncode != expected:
            raise AssertionError("转换CLI退出结果不符：" + result.stderr[-500:])
        if "Traceback" in result.stderr:
            raise AssertionError("CLI泄露了异常堆栈")
        return result

    def validate_document(self, result, source):
        self.assertEqual(set(result), {"protocol", "version", "unit", "sourceName", "sourceSha256", "parts"})
        self.assertEqual(result["protocol"], "equipment-cad-mesh")
        self.assertEqual(result["version"], 1)
        self.assertEqual(result["unit"], "mm")
        self.assertEqual(result["sourceName"], source.name)
        self.assertEqual(result["sourceSha256"], hashlib.sha256(source.read_bytes()).hexdigest())
        self.assertNotIn("equipmentId", result)
        self.assertEqual(len({part["id"] for part in result["parts"]}), len(result["parts"]))
        for part in result["parts"]:
            self.assertEqual(part["shape"], "mesh")
            self.assertEqual(part["material"], "未提供")
            self.assertTrue(part["name"].strip())
            self.assertTrue(all(math.isfinite(value) and value > 0 for value in part["dimensions"]))
            self.assertTrue(all(math.isfinite(value) for value in part["position"]))
            mesh = part["mesh"]
            self.assertGreater(len(mesh["vertices"]), 3)
            self.assertGreater(len(mesh["triangles"]), 1)
            for vertex in mesh["vertices"]:
                self.assertEqual(len(vertex), 3)
                self.assertTrue(all(math.isfinite(value) for value in vertex))
                self.assertTrue(all(abs(vertex[index]) <= part["dimensions"][index] / 2 + 1e-5 for index in range(3)))
            for triangle in mesh["triangles"]:
                self.assertEqual(len(triangle), 3)
                self.assertTrue(all(type(index) is int and 0 <= index < len(mesh["vertices"]) for index in triangle))
            self.assertGreater(signed_volume(mesh), 0, "闭合实体反向面须产生一致向外三角绕序")

    def test_real_protocol_names_finite_local_mesh_and_sha(self):
        self.validate_document(self.bearing, self.bearing_source)
        self.validate_document(self.motor, self.motor_source)
        self.assertEqual(len(self.bearing["parts"]), 1)
        self.assertGreater(len(self.motor["parts"]), 1)
        self.assertEqual(self.bearing["parts"][0]["name"], "608ZZ_Ball_Bearing")
        self.assertTrue(all(not part["name"].startswith("实体") for part in self.motor["parts"]))

    def test_bearing_real_curves_and_clear_central_hole(self):
        part = self.bearing["parts"][0]
        for actual, expected in zip(part["dimensions"], (22, 22, 7)):
            self.assertAlmostEqual(actual, expected, places=5)
        mesh = part["mesh"]
        self.assertGreater(len(mesh["vertices"]), 100)
        self.assertGreater(len({(round(vertex[0], 4), round(vertex[1], 4)) for vertex in mesh["vertices"]}), 20)
        self.assertLess(signed_volume(mesh), math.prod(part["dimensions"]) * 0.85)
        # 投影到XY后覆盖中心的非竖直三角意味着中心轴被封住；真实轴承孔不能被盒子替代。
        covers_axis = 0
        for triangle in mesh["triangles"]:
            a, b, c = [mesh["vertices"][index] for index in triangle]
            denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
            if abs(denominator) < 1e-10:
                continue
            alpha = ((b[1] - c[1]) * -c[0] + (c[0] - b[0]) * -c[1]) / denominator
            beta = ((c[1] - a[1]) * -c[0] + (a[0] - c[0]) * -c[1]) / denominator
            if min(alpha, beta, 1 - alpha - beta) >= -1e-8:
                covers_axis += 1
        self.assertEqual(covers_axis, 0)

    def test_motor_assembly_matches_independent_global_step_reader(self):
        from OCP.STEPControl import STEPControl_Reader
        from OCP.TopExp import TopExp_Explorer
        from OCP.TopAbs import TopAbs_SOLID
        from OCP.Bnd import Bnd_Box
        from OCP.BRepBndLib import BRepBndLib
        reader = STEPControl_Reader()
        self.assertEqual(reader.ReadFile(str(self.motor_source)), self.kernel["IFSelect_RetDone"])
        reader.SetSystemLengthUnit(1.0)
        self.assertGreater(reader.TransferRoots(), 0)
        explorer = TopExp_Explorer(reader.OneShape(), TopAbs_SOLID)
        expected = []
        while explorer.More():
            box = Bnd_Box(); BRepBndLib.AddOptimal_s(explorer.Current(), box, False, False)
            bounds = box.Get()
            expected.append(([bounds[index + 3] - bounds[index] for index in range(3)],
                             [(bounds[index + 3] + bounds[index]) / 2 for index in range(3)]))
            explorer.Next()
        self.assertEqual(len(expected), len(self.motor["parts"]))
        unmatched = list(expected)
        for part in self.motor["parts"]:
            match = min(unmatched, key=lambda item: sum(abs(a - b) for a, b in zip(item[1], part["position"])))
            for actual, wanted in zip(part["position"] + part["dimensions"], match[1] + match[0]):
                self.assertAlmostEqual(actual, wanted, places=5)
            unmatched.remove(match)
        self.assertGreater(len({tuple(round(value, 5) for value in part["position"]) for part in self.motor["parts"]}), 1)

    def test_source_units_metre_really_converted_to_millimetres(self):
        source = self.root / "单位受控米.step"
        original = self.bearing_source.read_text(encoding="utf8")
        self.assertIn("SI_UNIT(.MILLI.,.METRE.)", original)
        source.write_text(original.replace("SI_UNIT(.MILLI.,.METRE.)", "SI_UNIT($,.METRE.)"), encoding="utf8")
        output = self.root / "metres.json"
        self.run_conversion(source, output)
        data = json.loads(output.read_text(encoding="utf8"))
        for actual, expected in zip(data["parts"][0]["dimensions"], (22000, 22000, 7000)):
            self.assertAlmostEqual(actual, expected, places=3)
        self.assertEqual(data["unit"], "mm")

    def test_bad_input_and_no_solids_leave_no_output(self):
        bad = self.root / "格式坏.step"; bad.write_bytes(b"this is not STEP")
        bad_output = self.root / "bad.json"
        self.run_conversion(bad, bad_output, expected=1)
        self.assertFalse(bad_output.exists())
        self.run_conversion(self.bearing_source, bad_output)
        self.assertEqual(json.loads(bad_output.read_text(encoding="utf8"))["sourceSha256"], self.bearing["sourceSha256"],
                         "失败后同一目标可合法重试，不遗留输出占位")
        from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeFace
        from OCP.gp import gp_Pln, gp_Pnt, gp_Dir
        from OCP.STEPControl import STEPControl_Writer, STEPControl_AsIs
        face = BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), -2, 2, -3, 3).Face()
        writer = STEPControl_Writer()
        self.assertEqual(writer.Transfer(face, STEPControl_AsIs), self.kernel["IFSelect_RetDone"])
        surface = self.root / "开放曲面.step"
        self.assertEqual(writer.Write(str(surface)), self.kernel["IFSelect_RetDone"])
        surface_output = self.root / "surface.json"
        result = self.run_conversion(surface, surface_output, expected=1)
        self.assertIn("NO_SOLIDS", result.stderr)
        self.assertFalse(surface_output.exists())

    def test_repeat_and_racing_publish_never_overwrite_other_output(self):
        original = self.bearing_file.read_bytes()
        result = self.run_conversion(self.bearing_source, self.bearing_file, expected=1)
        self.assertIn("OUTPUT_EXISTS", result.stderr)
        self.assertEqual(self.bearing_file.read_bytes(), original)
        racing = self.root / "racing.json"; racing.write_bytes(b"user output")
        with self.assertRaises(self.module.ConversionError) as failure:
            self.module.publish_json({"fixture": 1}, racing)
        self.assertEqual(failure.exception.code, "OUTPUT_EXISTS")
        self.assertEqual(racing.read_bytes(), b"user output")
        self.assertFalse(list(self.root.glob("*.partial-*")))

    def test_mixed_solid_open_face_or_line_rejected_without_partial_import(self):
        from OCP.BRep import BRep_Builder
        from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox
        from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeFace, BRepBuilderAPI_MakeEdge
        from OCP.TopoDS import TopoDS_Compound
        from OCP.gp import gp_Pln, gp_Pnt, gp_Dir
        from OCP.STEPControl import STEPControl_Writer, STEPControl_AsIs
        box = BRepPrimAPI_MakeBox(2, 2, 2).Shape()
        open_face = BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(100, 0, 0), gp_Dir(1, 0, 0)), -2, 2, -3, 3).Face()
        line = BRepBuilderAPI_MakeEdge(gp_Pnt(100, 0, 0), gp_Pnt(100, 5, 0)).Edge()
        for suffix, unsupported in [("face", open_face), ("line", line)]:
            with self.subTest(kind=suffix):
                compound = TopoDS_Compound(); builder = BRep_Builder(); builder.MakeCompound(compound)
                builder.Add(compound, box); builder.Add(compound, unsupported)
                source = self.root / ("混合实体开放" + suffix + ".step")
                writer = STEPControl_Writer()
                self.assertEqual(writer.Transfer(compound, STEPControl_AsIs), self.kernel["IFSelect_RetDone"])
                self.assertEqual(writer.Write(str(source)), self.kernel["IFSelect_RetDone"])
                output = self.root / ("mixed-" + suffix + ".json")
                result = self.run_conversion(source, output, expected=1)
                self.assertIn("UNSUPPORTED_GEOMETRY", result.stderr)
                self.assertFalse(output.exists(), "不能成功发布只有box的半份模型")
                self.assertFalse(list(self.root.glob("*.partial-*")))
                output.write_bytes(b"existing user result")
                self.run_conversion(source, output, expected=1)
                self.assertEqual(output.read_bytes(), b"existing user result")
                output.unlink()  # 明确只删除本测试刚创建的虚构既有结果，供同target失败后合法重试。
                self.run_conversion(self.bearing_source, output)
                self.assertEqual(json.loads(output.read_text(encoding="utf8"))["sourceSha256"], self.bearing["sourceSha256"])

    def test_pure_multi_solid_compound_is_accepted(self):
        from OCP.BRep import BRep_Builder
        from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox
        from OCP.TopoDS import TopoDS_Compound
        from OCP.gp import gp_Pnt
        from OCP.STEPControl import STEPControl_Writer, STEPControl_AsIs
        compound = TopoDS_Compound(); builder = BRep_Builder(); builder.MakeCompound(compound)
        builder.Add(compound, BRepPrimAPI_MakeBox(2, 2, 2).Shape())
        builder.Add(compound, BRepPrimAPI_MakeBox(gp_Pnt(10, 0, 0), 3, 4, 5).Shape())
        source = self.root / "受控纯多实体.step"
        writer = STEPControl_Writer()
        self.assertEqual(writer.Transfer(compound, STEPControl_AsIs), self.kernel["IFSelect_RetDone"])
        self.assertEqual(writer.Write(str(source)), self.kernel["IFSelect_RetDone"])
        output = self.root / "multi-solid.json"
        self.run_conversion(source, output)
        result = json.loads(output.read_text(encoding="utf8"))
        self.assertEqual(len(result["parts"]), 2)
        self.validate_document(result, source)

    def test_original_nema_with_independent_open_shells_is_rejected(self):
        output = self.root / "original-nema.json"
        result = self.run_conversion(self.original_motor_source, output, expected=1)
        self.assertIn("UNSUPPORTED_GEOMETRY", result.stderr)
        self.assertFalse(output.exists(), "原NEMA的4个独立开放shell不能再只导入6solids")

    def test_solid_owned_repeated_faces_and_wires_do_not_false_reject(self):
        from OCP.BRep import BRep_Builder
        from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox
        from OCP.TopoDS import TopoDS_Compound
        from OCP.TopExp import TopExp, TopExp_Explorer
        from OCP.TopAbs import TopAbs_FACE, TopAbs_WIRE
        from OCP.TopTools import TopTools_IndexedMapOfShape
        solid = BRepPrimAPI_MakeBox(2, 2, 2).Shape()
        compound = TopoDS_Compound(); builder = BRep_Builder(); builder.MakeCompound(compound); builder.Add(compound, solid)
        builder.Add(compound, TopExp_Explorer(solid, TopAbs_FACE).Current())
        builder.Add(compound, TopExp_Explorer(solid, TopAbs_WIRE).Current())
        leaf, extra = self.module.leaf_solids(compound, self.kernel, self.module.time.monotonic())
        self.assertEqual(len(leaf), 1)
        covered = TopTools_IndexedMapOfShape(); TopExp.MapShapes_s(solid, covered, True, True)
        self.assertTrue(all(self.module.covered_by_solid(item, covered, self.kernel) for item in extra))

    def test_nested_assembly_rotated_placement_is_cumulative(self):
        from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox
        from OCP.gp import gp_Trsf, gp_Vec, gp_Ax1, gp_Pnt, gp_Dir
        from OCP.TopLoc import TopLoc_Location
        from OCP.STEPCAFControl import STEPCAFControl_Writer
        from OCP.STEPControl import STEPControl_AsIs
        document = self.kernel["TDocStd_Document"](self.kernel["TCollection_ExtendedString"]("BinXCAF"))
        tool = self.kernel["XCAFDoc_DocumentTool"].ShapeTool_s(document.Main())
        part = tool.AddShape(BRepPrimAPI_MakeBox(2, 4, 6).Shape(), False)
        lower = tool.NewShape()
        translated = gp_Trsf(); translated.SetTranslation(gp_Vec(10, 0, 0))
        occurrence = tool.AddComponent(lower, part, TopLoc_Location(translated))
        self.kernel["TDataStd_Name"].Set_s(occurrence, self.kernel["TCollection_ExtendedString"]("源实例叶"))
        upper = tool.NewShape()
        rotated = gp_Trsf(); rotated.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), math.pi / 2)
        rotated.SetTranslationPart(gp_Vec(100, 200, 300))
        tool.AddComponent(upper, lower, TopLoc_Location(rotated))
        tool.UpdateAssemblies()
        source = self.root / "受控嵌套旋转装配.step"
        writer = STEPCAFControl_Writer(); writer.SetNameMode(True)
        self.assertTrue(writer.Transfer(document, STEPControl_AsIs))
        self.assertEqual(writer.Write(str(source)), self.kernel["IFSelect_RetDone"])
        output = self.root / "nested.json"
        self.run_conversion(source, output)
        result = json.loads(output.read_text(encoding="utf8"))
        self.assertEqual(len(result["parts"]), 1)
        actual = result["parts"][0]
        for value, expected in zip(actual["position"], (98, 211, 303)):
            self.assertAlmostEqual(value, expected, places=5)
        for value, expected in zip(actual["dimensions"], (4, 2, 6)):
            self.assertAlmostEqual(value, expected, places=5)

    def test_limits_and_nonfinite_failure_remove_only_owned_partial(self):
        self.assertEqual((self.module.MAX_INPUT, self.module.MAX_SOLIDS, self.module.MAX_VERTICES,
                          self.module.MAX_TRIANGLES, self.module.MAX_OUTPUT), (100 * 1024 * 1024, 1000, 600000, 400000, 64 * 1024 * 1024))
        for constant, maximum, source, code in [("MAX_INPUT", 4, self.bearing_source, "INPUT_LIMIT"),
                ("MAX_SOLIDS", 1, self.motor_source, "SOLID_LIMIT"), ("MAX_VERTICES", 3, self.bearing_source, "MESH_LIMIT"),
                ("MAX_TRIANGLES", 1, self.bearing_source, "MESH_LIMIT")]:
            output = self.root / (constant + ".json")
            with patch.object(self.module, constant, maximum), self.assertRaises(self.module.ConversionError) as failure:
                self.module.convert(source, output, MODULES)
            self.assertEqual(failure.exception.code, code)
            self.assertFalse(output.exists())
        output = self.root / "output-limit.json"
        with patch.object(self.module, "MAX_OUTPUT", 8), self.assertRaises(self.module.ConversionError) as failure:
            self.module.publish_json({"fixture": "bounded"}, output)
        self.assertEqual(failure.exception.code, "OUTPUT_LIMIT")
        with self.assertRaises(self.module.ConversionError):
            self.module.finite([math.inf, 0, 0])
        with self.assertRaises(ValueError):
            self.module.publish_json({"fixture": math.nan}, self.root / "nan.json")
        self.assertFalse(output.exists())
        self.assertFalse(list(self.root.glob("*.partial-*")))
        deadline_output = self.root / "deadline.json"
        with patch.object(self.module.time, "monotonic", return_value=121), self.assertRaises(self.module.ConversionError) as failure:
            self.module.publish_json({"fixture": 1}, deadline_output, started=0)
        self.assertEqual(failure.exception.code, "TIME_LIMIT")
        self.assertFalse(deadline_output.exists())
        self.assertFalse(list(self.root.glob("*.partial-*")))

    def test_no_source_product_name_uses_explicit_entity_fallback(self):
        original = self.bearing_source.read_text(encoding="utf8")
        marker = "PRODUCT('608ZZ_Ball_Bearing','608ZZ_Ball_Bearing',''"
        self.assertIn(marker, original)
        source = self.root / "无源名字.step"
        source.write_text(original.replace(marker, "PRODUCT('','',''"), encoding="utf8")
        output = self.root / "unnamed.json"
        self.run_conversion(source, output)
        self.assertEqual(json.loads(output.read_text(encoding="utf8"))["parts"][0]["name"], "实体0001")

    def test_input_directory_cannot_supply_python_modules(self):
        poison = self.root / "OCP"; poison.mkdir()
        (poison / "__init__.py").write_text("raise RuntimeError('untrusted input directory module')", encoding="utf8")
        (self.root / "json.py").write_text("raise RuntimeError('untrusted json')", encoding="utf8")
        output = self.root / "isolated.json"
        self.run_conversion(self.bearing_source, output, cwd=self.root)
        self.assertEqual(json.loads(output.read_text(encoding="utf8"))["sourceSha256"], self.bearing["sourceSha256"])
        relative = self.run_conversion(self.bearing_source, self.root / "relative.json", expected=1, modules=Path("."), cwd=self.root)
        self.assertIn("MODULES_UNAVAILABLE", relative.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--modules", type=Path, default=MODULES)
    parser.add_argument("--samples", type=Path, default=SAMPLES)
    parser.add_argument("--assembly", type=Path, help="主管选择的公开纯闭合机械装配；缺省使用明确标记受控夹具")
    args = parser.parse_args()
    MODULES = args.modules.resolve()
    SAMPLES = args.samples.resolve()
    CLOSED_ASSEMBLY = args.assembly.resolve() if args.assembly else None
    unittest.main(argv=[sys.argv[0]], verbosity=2)
