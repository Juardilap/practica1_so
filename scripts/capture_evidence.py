#!/usr/bin/env python3
"""Capture real FIFO/SHM observations using private test builds and owned processes.

Run make test first. --commit identifies the source archived into this build.
The script never kills or removes IPC found outside its private session.
"""
import argparse
import ctypes
import datetime
import errno
import hashlib
import os
from pathlib import Path
import secrets
import select
import shlex
import shutil
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
LIBC = ctypes.CDLL(None, use_errno=True)
LIBC.shmget.argtypes = [ctypes.c_int, ctypes.c_size_t, ctypes.c_int]


def wait_for(predicate, label, timeout=5):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        value=predicate()
        if value: return value
        time.sleep(.01)
    raise RuntimeError('Timeout: '+label)


def read_until(process, marker):
    data=bytearray()
    deadline=time.monotonic()+5
    while marker not in data and time.monotonic()<deadline:
        if select.select([process.stdout],[],[],.1)[0]:
            part=os.read(process.stdout.fileno(),65536)
            if not part: break
            data.extend(part)
    if marker not in data: raise RuntimeError(f'Missing output: {marker!r}: {data!r}')
    return data.decode(errors='replace')


class Capture:
    def __init__(self, output, commit):
        self.output=output
        self.log=(output/'observations.txt').open('w')
        self.note(f'UTC: {datetime.datetime.now(datetime.timezone.utc).isoformat()}')
        self.note(f'Source commit: {commit}')
        self.note('Private test binaries: same sources, FIFO paths relative to temporary cwd; '
                  'SHM_TEST_BUILD key from SO_TEST_SHM_KEY, SHM_CLIENT_LOCK="shm-client.lock".')
        self.note('Dataset SHA256: '+hashlib.sha256((ROOT/'data/worldcities.csv').read_bytes()).hexdigest())
        self.run(['uname','-a']); self.run(['gcc','--version']); self.run(['ldd','--version'])

    def note(self, text):
        print(text,file=self.log,flush=True)

    def run(self, command, **kwargs):
        self.note('$ '+shlex.join([str(x) for x in command]))
        result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                              text=True,timeout=15,**kwargs)
        self.note(result.stdout); self.note(f'[exit {result.returncode}]')
        return result

    def observe(self, server, client=None):
        command=['python3',str(ROOT/'scripts/observe.py'),'--server-pid',str(server)]
        if client: command+=['--client-pid',str(client)]
        if self.run(command).returncode: raise RuntimeError('Observation failed')

    def session(self, transport):
        processes=[]; server_pid=None; tracer=None; shmid=None
        with tempfile.TemporaryDirectory(prefix=f'so-evidence-{transport}-') as directory:
            cwd=Path(directory); (cwd/'data').mkdir()
            shutil.copyfile(ROOT/'data/worldcities.csv',cwd/'data/worldcities.csv')
            env=dict(os.environ)
            key=secrets.randbelow(0x3fffffff)+0x40000000
            if transport=='shm':
                if LIBC.shmget(key,1,0)!=-1 or ctypes.get_errno()!=errno.ENOENT:
                    raise RuntimeError('Private key collision; rerun with a new random key')
                env['SO_TEST_SHM_KEY']=str(key)
                self.note(f'SO_TEST_SHM_KEY={key} ({key:#x})')
            trace=self.output/f'{transport}-strace.txt'
            events='openat,read,write,ppoll,rt_sigprocmask,close,unlink,flock' if transport=='fifo' else 'shmget,shmat,shmdt,shmctl,futex,pidfd_open,poll'
            command=['strace','-tt','-s','96','-o',str(trace),'-e','trace='+events,
                     str(ROOT/f'bin/servidor_{transport}_test')]
            self.note(f'\n=== {transport.upper()} cwd={cwd} ===')
            self.note('$ '+shlex.join(command))
            with (self.output/f'{transport}-server.txt').open('w') as server_log:
                try:
                    tracer=subprocess.Popen(command,cwd=cwd,env=env,stdout=server_log,stderr=server_log)
                    processes.append(tracer)
                    def child():
                        if tracer.poll() is not None: raise RuntimeError('strace/server exited before startup')
                        children=Path(f'/proc/{tracer.pid}/task/{tracer.pid}/children').read_text().split()
                        return int(children[0]) if children else None
                    server_pid=wait_for(child,'strace child')
                    self.note(f'strace PID={tracer.pid}; server PID={server_pid}')
                    if transport=='fifo':
                        wait_for(lambda: (cwd/'response.fifo').exists(),'FIFOs created')
                    else:
                        def created():
                            value=LIBC.shmget(key,1,0)
                            return value+1 if value>=0 else None
                        shmid=wait_for(created,'SHM created')-1
                        wait_for(lambda: 'Servidor SHM listo.' in (self.output/'shm-server.txt').read_text(),
                                 'SHM initialized')
                        self.run(['ipcs','-m','-i',str(shmid)])
                    self.note('--- Before connecting ---')
                    self.observe(server_pid)
                    client_command=[str(ROOT/f'bin/cliente_{transport}_test')]
                    self.note('$ '+shlex.join(client_command))
                    client=subprocess.Popen(client_command,cwd=cwd,env=env,stdin=subprocess.PIPE,
                                            stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                    processes.append(client)
                    self.note(f'client PID={client.pid}')
                    self.note(read_until(client,b'Opcion:'))
                    self.note('--- Connected, waiting for input ---')
                    self.observe(server_pid,client.pid)
                    if transport=='shm': self.run(['ipcs','-m','-i',str(shmid)])
                    self.note('stdin -> 1\\nTokyo\\n')
                    client.stdin.write(b'1\nTokyo\n'); client.stdin.flush()
                    self.note(read_until(client,b'Total coincidencias encontradas: 1'))
                    self.note('--- After Tokyo ---')
                    self.observe(server_pid,client.pid)
                    if transport=='fifo':
                        self.note('stdin -> 4\\n')
                        client.stdin.write(b'4\n'); client.stdin.flush()
                        out,err=client.communicate(timeout=5)
                        self.note(out.decode()); self.note(err.decode())
                        self.note(f'client exit={client.returncode}; server/strace exit={tracer.wait(timeout=5)}')
                        if client.returncode or tracer.returncode: raise RuntimeError('FIFO normal exit failed')
                        self.note('FIFO existence after exit: '+str([(p.name,p.exists()) for p in
                                  (cwd/'request.fifo',cwd/'response.fifo')]))
                        if (cwd/'request.fifo').exists() or (cwd/'response.fifo').exists():
                            raise RuntimeError('FIFO cleanup failed')
                    else:
                        self.note(f'$ kill -9 {server_pid}  # owned server launched above')
                        os.kill(server_pid,signal.SIGKILL)
                        self.note(f'server/strace exit={tracer.wait(timeout=5)}')
                        out,err=client.communicate(timeout=5)
                        self.note(out.decode()); self.note(err.decode()); self.note(f'client exit={client.returncode}')
                        if client.returncode!=1: raise RuntimeError('Client did not detect server death')
                        self.note('--- Residual segment after both detach ---')
                        self.run(['ipcs','-m','-i',str(shmid)])
                        cleanup=['python3',str(ROOT/'scripts/cleanup_ipc.py'),'--shmid',str(shmid),
                                 '--key',str(key),'--client-lock',str(cwd/'shm-client.lock')]
                        if self.run(cleanup).returncode: raise RuntimeError('Verified residual cleanup failed')
                        self.run(['ipcs','-m','-i',str(shmid)])
                        if LIBC.shmget(key,1,0)!=-1: raise RuntimeError('SHM key still present')
                        self.note('Verified shmget(private key): ENOENT')
                        shmid=None
                finally:
                    # Kill only children still owned by this live tracer/process object.
                    if tracer and tracer.poll() is None:
                        children=Path(f'/proc/{tracer.pid}/task/{tracer.pid}/children').read_text().split()
                        if server_pid and str(server_pid) in children:
                            os.kill(server_pid,signal.SIGKILL)
                    for process in reversed(processes):
                        if process.poll() is None: process.kill()
                        process.communicate(timeout=5)
                    if shmid is not None:
                        self.run(['python3',str(ROOT/'scripts/cleanup_ipc.py'),'--shmid',str(shmid),
                                  '--key',str(key),'--client-lock',str(cwd/'shm-client.lock')])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--commit',required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    for command in ('strace','ps','lsof','ipcs','ipcrm'):
        if not shutil.which(command): parser.error(f'Missing command: {command}')
    output=args.output.resolve(); output.mkdir(parents=True,exist_ok=False)
    capture=Capture(output,args.commit)
    try:
        capture.session('fifo'); capture.session('shm')
    finally: capture.log.close()
    print('Real observations saved:',output)


if __name__=='__main__':
    main()
