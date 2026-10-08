"""项目Redis环境初始化与有界原始字节桥；秘密只从setup stdin进入600配置。"""
import json
import os
from pathlib import Path
import selectors
import socket
import subprocess
import sys
import time

PURPOSE = "equipment-viewer-simulation-redis-v1"
DIRECTORY = Path("/opt/equipment-viewer-redis/runtime-6380")
BINARY = Path("/opt/equipment-viewer-redis/redis-7.4.7/src/redis-server")
CONFIG = DIRECTORY / "redis.local.conf"
PID_FILE = DIRECTORY / "redis.pid"
MARKER = DIRECTORY / "purpose.json"
BUFFER_LIMIT = 65536


def expected_marker():
    return {"purpose": PURPOSE, "port": 6380, "directory": str(DIRECTORY), "binary": str(BINARY)}


def check_marker():
    if json.loads(MARKER.read_text(encoding="utf8")) != expected_marker():
        raise RuntimeError("project marker mismatch")


def listening_inode():
    """只接受Linux loopback6380监听；未知端口不发认证或控制命令。"""
    target = "0100007F:%04X" % 6380
    for line in Path("/proc/net/tcp").read_text().splitlines()[1:]:
        fields = line.split()
        if fields[1].upper() == target and fields[3] == "0A":
            return fields[9]
    return None


def check_owned_pid():
    check_marker()
    value = PID_FILE.read_text(encoding="ascii").strip()
    if not value.isdigit() or int(value) < 2:
        raise RuntimeError("invalid pid")
    pid = int(value)
    if Path("/proc/%d/exe" % pid).resolve() != BINARY.resolve():
        raise RuntimeError("unknown process")
    if Path("/proc/%d/cwd" % pid).resolve() != DIRECTORY.resolve():
        raise RuntimeError("unknown process directory")
    inode = listening_inode()
    matching_socket = False
    for descriptor in Path("/proc/%d/fd" % pid).iterdir():
        try:
            matching_socket = matching_socket or os.readlink(descriptor) == "socket:[%s]" % inode
        except FileNotFoundError:
            pass  # 其他Redis连接并发关闭时fd可能消失，不把它误判为本监听socket。
    if not inode or not matching_socket:
        raise RuntimeError("unknown listener")
    return pid


def write_owned_file(filename, contents):
    descriptor = None
    owned = False
    complete = False
    try:
        descriptor = os.open(filename, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        owned = True
        with os.fdopen(descriptor, "w", encoding="utf8") as handle:
            descriptor = None
            handle.write(contents)
            handle.flush()
            os.fsync(handle.fileno())
        complete = True
    finally:
        if descriptor is not None:
            os.close(descriptor)
        if owned and not complete:
            Path(filename).unlink(missing_ok=True)


def resp_command(connection, *arguments):
    values = [str(argument).encode("utf8") for argument in arguments]
    payload = b"*%d\r\n" % len(values) + b"".join(b"$%d\r\n" % len(value) + value + b"\r\n" for value in values)
    connection.sendall(payload)
    stream = connection.makefile("rb")
    def line():
        value = stream.readline(BUFFER_LIMIT + 1)
        if not value.endswith(b"\r\n") or len(value) > BUFFER_LIMIT:
            raise RuntimeError("invalid RESP")
        return value[:-2]
    def read():
        value = line()
        if value.startswith(b"+"):
            return value[1:].decode("utf8")
        if value.startswith(b"$"):
            length = int(value[1:])
            if length < 0 or length > BUFFER_LIMIT:
                raise RuntimeError("RESP limit")
            result = stream.read(length + 2)
            if len(result) != length + 2 or not result.endswith(b"\r\n"):
                raise RuntimeError("incomplete RESP")
            return result[:-2].decode("utf8")
        if value.startswith(b"*"):
            count = int(value[1:])
            if count < 0 or count > 64:
                raise RuntimeError("RESP array limit")
            return [read() for _ in range(count)]
        raise RuntimeError("RESP failure")
    try:
        return read()
    finally:
        stream.close()


def authenticate_owned(password):
    pid = check_owned_pid()
    connection = socket.create_connection(("127.0.0.1", 6380), timeout=2)
    try:
        if resp_command(connection, "AUTH", password) != "OK":
            raise RuntimeError("authentication failed")
        if resp_command(connection, "PING") != "PONG":
            raise RuntimeError("readiness failed")
        server = dict(line.split(":", 1) for line in resp_command(connection, "INFO", "server").splitlines()
                      if line and not line.startswith("#") and ":" in line)
        if server.get("redis_version") != "7.4.7" or server.get("config_file") != str(CONFIG) or int(server["process_id"]) != pid:
            raise RuntimeError("server identity mismatch")
        directory = resp_command(connection, "CONFIG", "GET", "dir")
        if directory != ["dir", str(DIRECTORY)]:
            raise RuntimeError("server directory mismatch")
        return connection, pid
    except Exception:
        connection.close()
        raise


def setup(value):
    if value.get("purpose") != PURPOSE or value.get("linuxDirectory") != str(DIRECTORY) or value.get("port") != 6380:
        raise RuntimeError("invalid target")
    password = value.get("password", "")
    if len(password) != 64 or any(character not in "0123456789abcdef" for character in password):
        raise RuntimeError("invalid private config")
    operation = value.get("operation", "setup")
    if operation not in ("setup", "stop"):
        raise RuntimeError("invalid operation")
    marker_exists = MARKER.exists()
    if marker_exists:
        check_marker()
    contents = list(DIRECTORY.iterdir()) if DIRECTORY.exists() else []
    if not marker_exists and contents:
        raise RuntimeError("nonempty unmarked directory")
    if listening_inode() and not marker_exists:
        raise RuntimeError("unknown occupied port")
    if operation == "stop":
        if not marker_exists:
            raise RuntimeError("unmarked stop refused")
        if not listening_inode():
            return {"state": "not-listening", "port": 6380}
        connection, pid = authenticate_owned(password)
        try:
            # 只向已核实本实例发送SHUTDOWN NOSAVE，不kill未知PID或flush其他数据。
            values = [b"SHUTDOWN", b"NOSAVE"]
            connection.sendall(b"*2\r\n" + b"".join(b"$%d\r\n" % len(item) + item + b"\r\n" for item in values))
            connection.recv(1024)
        finally:
            connection.close()
        deadline = time.monotonic() + 5
        while listening_inode() and time.monotonic() < deadline:
            time.sleep(0.05)
        if listening_inode():
            raise RuntimeError("shutdown deadline")
        return {"state": "stopped", "port": 6380, "pid": pid}
    version = subprocess.run([str(BINARY), "--version"], check=True, capture_output=True, timeout=5).stdout.decode("ascii")
    if "v=7.4.7 " not in version:
        raise RuntimeError("unsupported binary")
    DIRECTORY.mkdir(parents=True, exist_ok=True)
    if not marker_exists:
        write_owned_file(MARKER, json.dumps(expected_marker()))
    expected = ("bind 127.0.0.1\nport 6380\nprotected-mode yes\nrequirepass %s\n"
                "daemonize yes\npidfile %s\nlogfile %s\ndir %s\n"
                "maxmemory 64mb\nmaxmemory-policy allkeys-lru\nsave \"\"\nappendonly no\n") % (
                    password, PID_FILE, DIRECTORY / "redis.log", DIRECTORY)
    if CONFIG.exists():
        if CONFIG.read_text(encoding="utf8") != expected or CONFIG.stat().st_mode & 0o077:
            raise RuntimeError("existing config mismatch")
    else:
        write_owned_file(CONFIG, expected)
    if not listening_inode():
        if PID_FILE.exists():
            pid = PID_FILE.read_text(encoding="ascii").strip()
            if not pid.isdigit() or Path("/proc/%s" % pid).exists():
                raise RuntimeError("live or unknown pid without listener")
        os.umask(0o077)
        subprocess.run([str(BINARY), str(CONFIG)], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=True, timeout=10)
    deadline = time.monotonic() + 5
    while not listening_inode() and time.monotonic() < deadline:
        time.sleep(0.05)
    connection, pid = authenticate_owned(password)
    try:
        memory = resp_command(connection, "CONFIG", "GET", "maxmemory")
        persistence = resp_command(connection, "CONFIG", "GET", "appendonly", "save")
        if memory != ["maxmemory", "67108864"] or dict(zip(persistence[::2], persistence[1::2])) != {"appendonly": "no", "save": ""}:
            raise RuntimeError("cache policy mismatch")
    finally:
        connection.close()
    return {"state": "ready", "port": 6380, "pid": pid, "version": "7.4.7", "maxmemory": 67108864, "persistence": False}


def pump(connection, input_fd=0, output_fd=1, idle_seconds=30, eof_seconds=1):
    """两方向各64KiB上限，按可写事件排空；EOF/断开/空闲均有界收尾。"""
    connection.setblocking(False)
    os.set_blocking(input_fd, False)
    os.set_blocking(output_fd, False)
    to_server = bytearray()
    to_output = bytearray()
    input_eof = False
    remote_eof = False
    write_shutdown = False
    eof_deadline = None
    last_activity = time.monotonic()
    selector = selectors.DefaultSelector()
    registered = {}
    def interest(target, mask, kind):
        previous = registered.get(target)
        if mask == 0:
            if previous is not None:
                selector.unregister(target)
                registered.pop(target, None)
        elif previous is None:
            selector.register(target, mask, kind)
            registered[target] = mask
        elif previous != mask:
            selector.modify(target, mask, kind)
            registered[target] = mask
    try:
        while True:
            now = time.monotonic()
            if now - last_activity > idle_seconds or (eof_deadline is not None and now >= eof_deadline):
                break
            if input_eof and not to_server and not write_shutdown:
                connection.shutdown(socket.SHUT_WR)
                write_shutdown = True
            if remote_eof and not to_output:
                break
            interest(input_fd, selectors.EVENT_READ if not input_eof and not remote_eof and len(to_server) < BUFFER_LIMIT else 0, "input")
            interest(output_fd, selectors.EVENT_WRITE if to_output else 0, "output")
            mask = (selectors.EVENT_READ if not remote_eof and len(to_output) < BUFFER_LIMIT else 0)
            if to_server and not remote_eof:
                mask |= selectors.EVENT_WRITE
            interest(connection, mask, "socket")
            for key, events in selector.select(0.1):
                if key.data == "input":
                    data = os.read(input_fd, min(16384, BUFFER_LIMIT - len(to_server)))
                    if data:
                        to_server.extend(data)
                        last_activity = time.monotonic()
                    else:
                        input_eof = True
                        eof_deadline = time.monotonic() + eof_seconds
                elif key.data == "output":
                    count = os.write(output_fd, to_output)
                    del to_output[:count]
                    last_activity = time.monotonic()
                else:
                    if events & selectors.EVENT_READ:
                        data = connection.recv(min(16384, BUFFER_LIMIT - len(to_output)))
                        if data:
                            to_output.extend(data)
                            last_activity = time.monotonic()
                        else:
                            remote_eof = True
                            input_eof = True
                            to_server.clear()
                            eof_deadline = time.monotonic() + eof_seconds
                    if events & selectors.EVENT_WRITE and not remote_eof:
                        count = connection.send(to_server)
                        del to_server[:count]
                        last_activity = time.monotonic()
    finally:
        selector.close()
        connection.close()


def main():
    if sys.argv[1:] == ["--setup"]:
        value = json.loads(sys.stdin.buffer.read(32769))
        result = setup(value)
        sys.stdout.write(json.dumps(result) + "\n")
        return
    if sys.argv[1:] != ["127.0.0.1", "6380"]:
        raise RuntimeError("invalid bridge target")
    check_owned_pid()  # 在认证字节转发前证明监听socket由专属PID拥有。
    connection = socket.create_connection(("127.0.0.1", 6380), timeout=2)
    pump(connection)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        # stdout只承载RESP/安全setup结果，错误不包含密码或待转发数据。
        sys.stderr.write("Project Redis operation failed.\n")
        sys.exit(1)
