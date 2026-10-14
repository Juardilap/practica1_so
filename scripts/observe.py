#!/usr/bin/env python3
"""Read-only Linux observations; run strace at process launch separately."""
import argparse
from pathlib import Path
import platform
import subprocess


def command(args):
    print('$ ' + ' '.join(args), flush=True)
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(result.stdout, end='', flush=True)
    return result.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server-pid', type=int, required=True)
    parser.add_argument('--client-pid', type=int)
    args = parser.parse_args()
    print('Kernel:', platform.platform())
    for pid in (args.server_pid, args.client_pid):
        if pid is None:
            continue
        if pid <= 0:
            parser.error('PID debe ser positivo')
        command(['ps', '-o', 'pid,ppid,stat,wchan,cmd', '-p', str(pid)])
        for name in ('status', 'wchan'):
            path = Path('/proc') / str(pid) / name
            print(f'$ cat {path}')
            try:
                print(path.read_text())
            except OSError as exc:
                print(exc)
        command(['ls', '-l', f'/proc/{pid}/fd'])
        command(['lsof', '-p', str(pid)])
    command(['ipcs', '-m'])


if __name__ == '__main__':
    main()
