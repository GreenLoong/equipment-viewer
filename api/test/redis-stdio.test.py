"""纯proc替身身份回归，不运行WSL、Redis或读取真实/proc。"""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch, Mock

source = Path(__file__).parents[1] / "tools" / "redis-stdio.py"
specification = importlib.util.spec_from_file_location("project_redis_stdio", source)
module = importlib.util.module_from_spec(specification)
specification.loader.exec_module(module)


class FakeProcPath:
    def __init__(self, path, cwd):
        self.path = path
        self.cwd = cwd

    def resolve(self):
        return "/fixed/binary" if self.path.endswith("/exe") else self.cwd

    def iterdir(self):
        return ["fake-fd"]


class OwnedProcessTests(unittest.TestCase):
    def run_identity(self, cwd):
        with patch.object(module, "check_marker"), patch.object(module, "PID_FILE", Mock(read_text=lambda **_options: "321")), \
                patch.object(module, "BINARY", Mock(resolve=lambda: "/fixed/binary")), \
                patch.object(module, "DIRECTORY", Mock(resolve=lambda: "/fixed/runtime")), \
                patch.object(module, "Path", lambda path: FakeProcPath(path, cwd)), \
                patch.object(module, "listening_inode", return_value="123"), \
                patch.object(module.os, "readlink", return_value="socket:[123]"):
            return module.check_owned_pid()

    def test_same_binary_port_and_inode_but_other_cwd_refused(self):
        with self.assertRaisesRegex(RuntimeError, "directory"):
            self.run_identity("/other/runtime")

    def test_matching_binary_cwd_and_listener_accepted(self):
        self.assertEqual(self.run_identity("/fixed/runtime"), 321)


if __name__ == "__main__":
    unittest.main()
