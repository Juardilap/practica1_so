"""SHM acceptance tests: private random keys, cwd, and owned shmid cleanup."""
import ctypes
import errno
import os
from pathlib import Path
import secrets
import select
import signal
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / 'bin/servidor_shm_test'
CLIENT = ROOT / 'bin/cliente_shm_test'
PROBE = ROOT / 'bin/shm_probe'
FAULTS = ROOT / 'bin/shm_faults.so'
LIBC = ctypes.CDLL(None, use_errno=True)
LIBC.shmget.argtypes = [ctypes.c_int, ctypes.c_size_t, ctypes.c_int]
LIBC.shmctl.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_void_p]


class ShmTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = (ROOT / 'data/worldcities.csv').read_bytes()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='so-shm-tests-')
        self.cwd = Path(self.temp.name)
        # The dynamic loader splits LD_PRELOAD on spaces, including spaces in paths.
        self.faults = self.cwd / 'shm_faults.so'
        self.faults.write_bytes(FAULTS.read_bytes())
        (self.cwd / 'data').mkdir()
        (self.cwd / 'data/worldcities.csv').write_bytes(self.data)
        self.key = secrets.randbelow(0x3fffffff) + 0x40000000
        self.assertEqual(LIBC.shmget(self.key, 1, 0), -1)
        self.assertEqual(ctypes.get_errno(), errno.ENOENT)
        self.env = dict(os.environ, SO_TEST_SHM_KEY=str(self.key))
        self.processes = []
        self.ids = set()

    def tearDown(self):
        for p in self.processes:
            if p.poll() is None:
                p.kill()
            p.communicate(timeout=3)
            for stream in (p.stdin, p.stdout, p.stderr):
                if stream:
                    stream.close()
        for shmid in self.ids:
            if LIBC.shmctl(shmid, 0, None) != 0:
                self.assertIn(ctypes.get_errno(), (errno.EINVAL, errno.EIDRM))
        self.temp.cleanup()

    def launch(self, executable, *args, extra=None):
        env = dict(self.env, **(extra or {}))
        p = subprocess.Popen([str(executable), *args], cwd=self.cwd, env=env,
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.processes.append(p)
        return p

    def wait_for(self, predicate, label):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(.005)
        self.fail('Timeout: ' + label)

    def read_until(self, p, marker, timeout=4):
        data = bytearray()
        deadline = time.monotonic() + timeout
        while marker not in data and time.monotonic() < deadline:
            if select.select([p.stdout], [], [], .1)[0]:
                chunk = os.read(p.stdout.fileno(), 65536)
                if not chunk:
                    break
                data.extend(chunk)
        self.assertIn(marker, data)
        return data

    def server(self, extra=None, ready=True):
        server = self.launch(SERVER, extra=extra)
        def created():
            shmid = LIBC.shmget(self.key, 1, 0)
            if shmid >= 0:
                self.ids.add(shmid)
                return True
            return server.poll() is not None
        self.wait_for(created, 'segment creation')
        self.assertIsNone(server.poll())
        if ready:
            self.read_until(server, b'Servidor SHM listo.')
        return server

    def finish(self, p, code=0, timeout=4):
        out, err = p.communicate(timeout=timeout)
        self.assertEqual(p.returncode, code, err.decode(errors='replace'))
        return out, err

    def absent(self):
        self.assertEqual(LIBC.shmget(self.key, 1, 0), -1)
        self.assertEqual(ctypes.get_errno(), errno.ENOENT)

    def test_batch_and_nine_searches(self):
        server = self.server()
        client = self.launch(CLIENT)
        queries = [(1,'AtlantisCity',0),(1,'Tokyo',1),(2,'Colombia',714),
                   (1,'tokyo',1),(1,'Bogota',2),(2,'colombia',714),(3,'CO',714),
                   (3,'JP',1253),(2,'PaisInventado',0)]
        payload = ''.join(f'{op}\n{value}\n' for op,value,_ in queries) + '4\n'
        out, err = client.communicate(payload.encode(), timeout=8)
        self.assertEqual(client.returncode, 0, err)
        self.assertEqual(out.count(b'Ciudad:'), sum(q[2] for q in queries))
        import re
        self.assertEqual([int(n) for n in re.findall(rb'Total coincidencias encontradas: (\d+)',out)],
                         [q[2] for q in queries])
        self.finish(server)
        self.absent()

    def test_gradual_queries(self):
        server = self.server()
        client = self.launch(CLIENT)
        for command, count in [(b'1\nAtlantisCity\n',0),(b'1\nTokyo\n',1),(b'2\nColombia\n',714)]:
            client.stdin.write(command); client.stdin.flush()
            self.read_until(client, f'Total coincidencias encontradas: {count}\n'.encode())
        client.stdin.write(b'4\n'); client.stdin.flush()
        self.finish(client); self.finish(server); self.absent()

    def test_eof_last_line_and_oversize(self):
        for payload, rows in [(b'',0),(b'1\n',0),(b'1\nTokyo',1),(b'4',0),
                              (b'9\n' + b'x'*5000+b'\n1\n'+b'y'*5000+b'\n1\nTokyo\n4\n',1)]:
            with self.subTest(payload=payload[:20]):
                server = self.server()
                client = self.launch(CLIENT)
                out, err = client.communicate(payload, timeout=4)
                self.assertEqual(client.returncode, 0, err)
                self.assertEqual(out.count(b'Ciudad:'), rows)
                self.finish(server); self.absent()

    def test_dataset_errors_terminal(self):
        path = self.cwd / 'data/worldcities.csv'
        for content in (None, b'bad,row\n', self.data.splitlines(keepends=True)[0]+b'bad,row\n'):
            with self.subTest(content=content):
                if path.exists(): path.unlink()
                if content is not None: path.write_bytes(content)
                server = self.server()
                client = self.launch(CLIENT)
                out, err = client.communicate(b'1\nTokyo\n4\n', timeout=4)
                self.assertEqual(client.returncode, 1, err)
                self.assertNotIn(b'Busqueda finalizada',out)
                self.assertNotIn(b'Sesion finalizada',out)
                self.finish(server,1); self.absent()

    def test_second_server_and_client(self):
        server = self.server()
        client = self.launch(CLIENT)
        self.read_until(client,b'Opcion:')
        original = LIBC.shmget(self.key,1,0)
        self.finish(self.launch(SERVER),1)
        self.finish(self.launch(CLIENT),1)
        self.assertEqual(LIBC.shmget(self.key,1,0),original)
        client.stdin.write(b'4\n'); client.stdin.flush()
        self.finish(client); self.finish(server); self.absent()

    def test_delayed_initialization(self):
        server = self.server({'LD_PRELOAD':str(self.faults),'SO_TEST_INIT_DELAY_MS':'500'},ready=False)
        client = self.launch(CLIENT)
        out, err = client.communicate(b'1\nTokyo\n4\n',timeout=4)
        self.assertEqual(client.returncode,0,err)
        self.assertIn(b'Total coincidencias encontradas: 1',out)
        self.finish(server); self.absent()

    def test_initialization_timeout(self):
        server = self.server({'LD_PRELOAD':str(self.faults),'SO_TEST_INIT_DELAY_MS':'6000'},ready=False)
        client = self.launch(CLIENT)
        self.finish(client,1,timeout=7)
        server.terminate(); self.finish(server); self.absent()

    def test_partial_initialization_failures(self):
        for step in (1,2,3):
            with self.subTest(step=step):
                server = self.server({'LD_PRELOAD':str(self.faults),'SO_TEST_INIT_DELAY_MS':'200',
                                      'SO_TEST_FAIL_INIT':str(step)},ready=False)
                client = self.launch(CLIENT)
                self.finish(client,1); self.finish(server,1); self.absent()

    def test_incompatible_segment_and_version(self):
        shmid=LIBC.shmget(self.key,1,0o1000|0o2000|0o660)
        self.assertGreaterEqual(shmid,0); self.ids.add(shmid)
        self.finish(self.launch(CLIENT),1)
        self.finish(self.launch(SERVER),1)
        self.assertEqual(LIBC.shmget(self.key,1,0),shmid)
        self.assertEqual(LIBC.shmctl(shmid,0,None),0)
        self.ids.remove(shmid)
        server=self.server()
        shmid=LIBC.shmget(self.key,1,0)
        self.finish(self.launch(PROBE,'corrupt',str(shmid)))
        self.finish(self.launch(CLIENT),1)
        server.terminate(); self.finish(server); self.absent()

    def test_invalid_requests(self):
        for operation,value in [('99','Tokyo'),('1','badnul')]:
            with self.subTest(op=operation,value=value):
                server=self.server()
                probe=self.launch(PROBE,operation,value)
                out,_=self.finish(probe)
                self.assertIn(b'TYPE 4',out)
                self.assertNotIn(b'TYPE 2',out)
                self.finish(server,1); self.absent()

    def test_server_signals_idle_and_connected(self):
        for signo in (signal.SIGINT,signal.SIGTERM):
            for connected in (False,True):
                with self.subTest(signal=signo,connected=connected):
                    server=self.server()
                    if connected:
                        client=self.launch(CLIENT)
                        self.read_until(client,b'Opcion:')
                    server.send_signal(signo)
                    if connected: self.finish(client,1)
                    self.finish(server); self.absent()

    def test_client_signals_at_prompt(self):
        for signo in (signal.SIGINT,signal.SIGTERM):
            with self.subTest(signal=signo):
                server=self.server(); client=self.launch(CLIENT)
                self.read_until(client,b'Opcion:')
                client.send_signal(signo)
                self.finish(client); self.finish(server,1); self.absent()

    def test_client_signals_waiting_response(self):
        for signo in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signal=signo):
                server=self.server(); client=self.launch(CLIENT)
                self.read_until(client,b'Opcion:')
                server.send_signal(signal.SIGSTOP)
                self.wait_for(lambda: Path(f'/proc/{server.pid}/stat').read_text().split(') ')[1][0]=='T',
                              'server stopped')
                client.stdin.write(b'1\nTokyo\n'); client.stdin.flush()
                self.wait_for(lambda: 'futex' in Path(f'/proc/{client.pid}/wchan').read_text(),
                              'client awaiting response')
                client.send_signal(signo)
                self.finish(client)
                server.send_signal(signal.SIGCONT)
                self.finish(server,1); self.absent()

    def test_cleanup_rejects_active_and_wrong_key(self):
        server=self.server()
        shmid=LIBC.shmget(self.key,1,0)
        command=['python3',str(ROOT/'scripts/cleanup_ipc.py'),'--shmid',str(shmid),
                 '--client-lock',str(self.cwd/'shm-client.lock'),'--key',str(self.key)]
        active=subprocess.run(command,capture_output=True,timeout=3)
        self.assertEqual(active.returncode,1)
        self.assertEqual(LIBC.shmget(self.key,1,0),shmid)
        client=self.launch(CLIENT); self.read_until(client,b'Opcion:')
        locked=subprocess.run(command,capture_output=True,timeout=3)
        self.assertEqual(locked.returncode,1)
        server.kill(); self.finish(server,-signal.SIGKILL); self.finish(client,1)
        wrong=subprocess.run(command[:-1]+[str(self.key+1)],capture_output=True,timeout=3)
        self.assertEqual(wrong.returncode,1)
        self.assertEqual(LIBC.shmget(self.key,1,0),shmid)
        cleaned=subprocess.run(command,capture_output=True,timeout=3)
        self.assertEqual(cleaned.returncode,0,cleaned.stderr)
        self.ids.remove(shmid); self.absent()

    def test_death_during_request_or_ack(self):
        for phase in ('idle','row_ack','bye_ack'):
            for victim in ('client','server'):
                with self.subTest(phase=phase,victim=victim):
                    server=self.server()
                    args=('hold',) if phase=='idle' else (('2','Colombia','noack') if phase=='row_ack' else ('4','','noack'))
                    client=self.launch(PROBE,*args)
                    self.read_until(client,b'CONNECTED' if phase=='idle' else b'TYPE ')
                    if victim=='client':
                        client.kill(); self.finish(client,-signal.SIGKILL)
                        self.finish(server,1); self.absent()
                    else:
                        # Real client has timed waits; probe deliberately pauses for ACK tests.
                        server.kill(); self.finish(server,-signal.SIGKILL)
                        shmid=LIBC.shmget(self.key,1,0)
                        self.assertGreaterEqual(shmid,0)
                        client.kill(); self.finish(client,-signal.SIGKILL)
                        self.finish(self.launch(CLIENT),1) # residual creator is dead
                        self.assertEqual(LIBC.shmctl(shmid,0,None),0)
                        self.ids.remove(shmid); self.absent()

    def test_real_client_detects_server_death(self):
        for querying in (False,True):
            with self.subTest(querying=querying):
                server=self.server(); client=self.launch(CLIENT)
                self.read_until(client,b'Opcion:')
                if querying:
                    server.send_signal(signal.SIGSTOP)
                    client.stdin.write(b'1\nTokyo\n'); client.stdin.flush()
                    time.sleep(.05)
                server.kill(); self.finish(server,-signal.SIGKILL)
                self.finish(client,1)
                shmid=LIBC.shmget(self.key,1,0)
                self.assertGreaterEqual(shmid,0)
                self.assertEqual(LIBC.shmctl(shmid,0,None),0)
                self.ids.remove(shmid); self.absent()

    def test_signals_while_waiting_ack(self):
        for signo in (signal.SIGINT,signal.SIGTERM):
            for op,value in [('2','Colombia'),('4','')]:
                with self.subTest(signal=signo,operation=op):
                    server=self.server(); probe=self.launch(PROBE,op,value,'noack')
                    self.read_until(probe,b'TYPE ')
                    server.send_signal(signo)
                    # Peer is deliberately stuck: no sem_destroy until safe; bounded shutdown.
                    _,err=self.finish(server,1)
                    self.assertIn(b'destruccion omitida',err)
                    probe.kill(); self.finish(probe,-signal.SIGKILL)
                    self.absent()

    def test_signal_during_dataset_read(self):
        path=self.cwd/'data/worldcities.csv'; path.unlink(); os.mkfifo(path)
        for signo in (signal.SIGINT,signal.SIGTERM):
            with self.subTest(signal=signo):
                server=self.server(); client=self.launch(CLIENT)
                client.stdin.write(b'1\nTokyo\n'); client.stdin.flush()
                opened=[]
                def writer_ready():
                    try: opened.append(os.open(path,os.O_WRONLY|os.O_NONBLOCK)); return True
                    except OSError as exc:
                        if exc.errno!=errno.ENXIO: raise
                        return False
                self.wait_for(writer_ready,'CSV read')
                try:
                    os.write(opened[0], self.data.splitlines(keepends=True)[0])
                    self.wait_for(lambda: 'pipe_read' in Path(f'/proc/{server.pid}/wchan').read_text(),
                                  'server blocked reading CSV')
                    server.send_signal(signo)
                    self.finish(client,1); self.finish(server); self.absent()
                finally: os.close(opened[0])


if __name__=='__main__':
    unittest.main(verbosity=2)
