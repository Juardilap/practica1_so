"""FIFO acceptance tests. All IPC paths live in private temporary directories."""
import os
from pathlib import Path
import select
import signal
import struct
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / "bin/servidor_fifo_test"
CLIENT = ROOT / "bin/cliente_fifo_test"
HEADER = "city,city_ascii,lat,lng,country,iso2,iso3,admin_name,capital,population,id\n"
ROW = "Tokyo,Tokyo,1,2,Japan,JP,JPN,Region,,100,1\n"
REQUEST = struct.Struct("=i128s")
RESPONSE = struct.Struct("=i512s")


class FifoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Snapshot real bytes once; /mnt/c latency must not become IPC timeouts.
        cls.dataset_bytes = (ROOT / "data/worldcities.csv").read_bytes()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="so-fifo-tests-")
        self.cwd = Path(self.temp.name)
        (self.cwd / "data").mkdir()
        (self.cwd / "data/worldcities.csv").write_bytes(self.dataset_bytes)
        self.processes = []
        self.fds = []

    def tearDown(self):
        for fd in self.fds:
            os.close(fd)
        for p in self.processes:
            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    p.kill()
            p.communicate(timeout=2)
            for stream in (p.stdin, p.stdout, p.stderr):
                if stream is not None:
                    stream.close()
        self.temp.cleanup()

    def wait_for(self, predicate, description):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.005)
        self.fail("Timeout: " + description)

    def start(self, executable):
        p = subprocess.Popen([str(executable)], cwd=self.cwd, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.processes.append(p)
        return p

    def server(self):
        p = self.start(SERVER)
        self.wait_for(lambda: (self.cwd / "response.fifo").exists() or p.poll() is not None,
                      "server startup")
        self.assertIsNone(p.poll())
        return p

    def finish(self, p, expected=0):
        output, error = p.communicate(timeout=4)
        self.assertEqual(p.returncode, expected, error.decode(errors="replace"))
        return output, error

    def cleaned(self):
        self.assertFalse((self.cwd / "request.fifo").exists())
        self.assertFalse((self.cwd / "response.fifo").exists())
        self.assertTrue((self.cwd / "server.lock").is_file())

    def dataset(self, content):
        path = self.cwd / "data/worldcities.csv"
        path.unlink()
        path.write_text(content)

    def raw(self):
        # open writer only once the server has opened its read endpoint
        result = []
        def connect():
            try:
                result.append(os.open(self.cwd / "request.fifo", os.O_WRONLY | os.O_NONBLOCK))
                return True
            except OSError as exc:
                if exc.errno != 6:  # ENXIO: startup still in progress
                    raise
                return False
        self.wait_for(connect, "raw request endpoint")
        req = result[0]
        resp = os.open(self.cwd / "response.fifo", os.O_RDONLY | os.O_NONBLOCK)
        self.fds.extend((req, resp))
        return req, resp

    def send(self, req, op, value=""):
        self.assertEqual(os.write(req, REQUEST.pack(op, value.encode())), REQUEST.size)

    def receive(self, resp):
        data = bytearray()
        deadline = time.monotonic() + 3
        while len(data) < RESPONSE.size and time.monotonic() < deadline:
            if not select.select([resp], [], [], 0.05)[0]:
                continue
            part = os.read(resp, RESPONSE.size - len(data))
            if not part:
                time.sleep(0.005)  # response writer may still be connecting
            data.extend(part)
        self.assertEqual(len(data), RESPONSE.size)
        kind, message = RESPONSE.unpack(data)
        return kind, message.split(b"\0", 1)[0]

    def close_raw(self, *fds):
        for fd in fds:
            os.close(fd)
            self.fds.remove(fd)

    def read_until(self, p, marker):
        data = bytearray()
        deadline = time.monotonic() + 4
        while marker not in data and time.monotonic() < deadline:
            if select.select([p.stdout], [], [], 0.1)[0]:
                chunk = os.read(p.stdout.fileno(), 65536)
                if not chunk:
                    break
                data.extend(chunk)
        self.assertIn(marker, data)
        return data

    def test_batch_queries(self):
        server = self.server()
        client = self.start(CLIENT)
        output, error = client.communicate(b"1\nAtlantisCity\n1\nTokyo\n2\nColombia\n4\n", timeout=5)
        self.assertEqual(client.returncode, 0, error)
        self.assertEqual(output.count(b"Ciudad:"), 715)
        for n in (0, 1, 714):
            self.assertIn(f"Total coincidencias encontradas: {n}\n".encode(), output)
        self.finish(server)
        self.cleaned()

    def test_gradual_queries(self):
        server = self.server()
        client = self.start(CLIENT)
        output = bytearray()
        for command, count in [(b"1\nAtlantisCity\n", 0), (b"1\nTokyo\n", 1), (b"2\nColombia\n", 714)]:
            client.stdin.write(command)
            client.stdin.flush()
            output.extend(self.read_until(client, f"Total coincidencias encontradas: {count}\n".encode()))
        client.stdin.write(b"4\n")
        client.stdin.flush()
        self.finish(client)
        self.assertEqual(output.count(b"Ciudad:"), 715)
        self.finish(server)
        self.cleaned()

    def test_eof_and_last_line(self):
        for payload, rows in [(b"", 0), (b"1\n", 0), (b"1\nTokyo", 1), (b"4", 0)]:
            with self.subTest(payload=payload):
                server = self.server()
                client = self.start(CLIENT)
                output, error = client.communicate(payload, timeout=4)
                self.assertEqual(client.returncode, 0, error)
                self.assertEqual(output.count(b"Ciudad:"), rows)
                self.finish(server)
                self.cleaned()

    def test_oversize_and_invalid_input_preserve_next_lines(self):
        server = self.server()
        client = self.start(CLIENT)
        payload = b"9\n" + b"x" * 5000 + b"\n1\n" + b"y" * 5000 + b"\n1\nTokyo\n4\n"
        output, error = client.communicate(payload, timeout=4)
        self.assertEqual(client.returncode, 0, error)
        self.assertEqual(output.count(b"Ciudad:"), 1)
        self.assertEqual(error.count(b"Entrada demasiado larga"), 2)
        self.finish(server)
        self.cleaned()

    def test_dataset_error_is_terminal(self):
        self.dataset(HEADER + "bad,row\n")
        server = self.server()
        client = self.start(CLIENT)
        output, error = client.communicate(b"1\nTokyo\n4\n", timeout=4)
        self.assertEqual(client.returncode, 1)
        self.assertIn(b"linea 2", error)
        self.assertNotIn(b"Busqueda finalizada", output)
        self.finish(server, 1)
        self.cleaned()

    def test_missing_dataset_is_terminal(self):
        (self.cwd / "data/worldcities.csv").unlink()
        server = self.server()
        client = self.start(CLIENT)
        output, error = client.communicate(b"1\nTokyo\n", timeout=4)
        self.assertEqual(client.returncode, 1)
        self.assertIn(b"abrir dataset", error)
        self.assertNotIn(b"Busqueda finalizada", output)
        self.finish(server, 1)
        self.cleaned()

    def test_reused_fifos_and_second_server(self):
        for name in ("request.fifo", "response.fifo"):
            os.mkfifo(self.cwd / name, 0o660)
        server = self.server()
        inodes = [(self.cwd / name).stat().st_ino for name in ("request.fifo", "response.fifo")]
        second = self.start(SERVER)
        self.finish(second, 1)
        self.assertEqual(inodes, [(self.cwd / name).stat().st_ino for name in ("request.fifo", "response.fifo")])
        req, resp = self.raw()
        self.send(req, 4)
        self.assertEqual(self.receive(resp)[0], 5)
        self.finish(server)
        self.cleaned()

    def test_foreign_file_and_symlink_preserved(self):
        foreign = self.cwd / "foreign"
        foreign.write_text("preserve")
        path = self.cwd / "request.fifo"
        for symlink in (False, True):
            with self.subTest(symlink=symlink):
                if symlink:
                    path.symlink_to(foreign)
                else:
                    path.write_text("preserve")
                self.finish(self.start(SERVER), 1)
                self.assertEqual(path.read_text(), "preserve")
                self.assertEqual(foreign.read_text(), "preserve")
                path.unlink()

    def test_replaced_fifo_not_unlinked(self):
        server = self.server()
        req, resp = self.raw()
        path = self.cwd / "request.fifo"
        path.unlink()
        path.write_text("replacement")
        self.send(req, 4)
        self.assertEqual(self.receive(resp)[0], 5)
        self.finish(server, 1)
        self.assertEqual(path.read_text(), "replacement")
        self.assertFalse((self.cwd / "response.fifo").exists())

    def test_fragmented_request_and_partial_eof(self):
        server = self.server()
        req, resp = self.raw()
        packet = REQUEST.pack(1, b"Tokyo")
        for i in range(0, len(packet), 7):
            os.write(req, packet[i:i + 7])
            time.sleep(0.001)
        self.assertEqual(self.receive(resp)[0], 1)
        self.assertEqual(self.receive(resp)[0], 2)
        os.write(req, packet[:15])
        self.close_raw(req)
        self.finish(server, 1)
        self.cleaned()

    def test_disconnect_during_response(self):
        server = self.server()
        req, resp = self.raw()
        self.send(req, 2, "Colombia")
        self.assertEqual(self.receive(resp)[0], 1)
        self.close_raw(req, resp)
        self.finish(server, 1)
        self.cleaned()

    def test_signals_before_connection(self):
        for signo in (signal.SIGINT, signal.SIGTERM):
            for half_connected in (False, True):
                with self.subTest(signal=signo, half_connected=half_connected):
                    server = self.server()
                    if half_connected:
                        # Open both endpoints, then drop response before server connects.
                        # Initial wait itself is exercised independently above.
                        req, resp = self.raw()
                        self.close_raw(resp)
                    server.send_signal(signo)
                    self.finish(server)
                    if half_connected:
                        self.close_raw(req)
                    self.cleaned()

    def test_signals_waiting_request_and_backpressure(self):
        for signo in (signal.SIGINT, signal.SIGTERM):
            for phase in ("idle", "partial", "full_response_pipe"):
                with self.subTest(signal=signo, phase=phase):
                    server = self.server()
                    req, resp = self.raw()
                    if phase == "partial":
                        os.write(req, REQUEST.pack(1, b"Tokyo")[:20])
                    elif phase == "full_response_pipe":
                        self.send(req, 2, "Colombia")
                        self.assertEqual(self.receive(resp)[0], 1)
                    time.sleep(0.1)
                    server.send_signal(signo)
                    self.finish(server)
                    self.close_raw(req, resp)
                    self.cleaned()

    def test_signals_during_dataset_read(self):
        path = self.cwd / "data/worldcities.csv"
        path.unlink()
        os.mkfifo(path)
        for signo in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signal=signo):
                server = self.server()
                req, resp = self.raw()
                self.send(req, 1, "Tokyo")
                writer = []
                def connect_dataset():
                    try:
                        writer.append(os.open(path, os.O_WRONLY | os.O_NONBLOCK))
                        return True
                    except OSError as exc:
                        if exc.errno != 6:
                            raise
                        return False
                self.wait_for(connect_dataset, "dataset opened for search")
                self.fds.append(writer[0])
                os.write(writer[0], (HEADER + ROW).encode())
                self.assertEqual(self.receive(resp)[0], 1)
                server.send_signal(signo)
                self.finish(server)
                self.close_raw(req, resp, writer[0])
                self.cleaned()

    def test_invalid_request_is_terminal(self):
        server = self.server()
        req, resp = self.raw()
        self.send(req, 99)
        self.assertEqual(self.receive(resp)[0], 4)
        self.finish(server, 1)
        self.cleaned()


if __name__ == "__main__":
    unittest.main(verbosity=2)
