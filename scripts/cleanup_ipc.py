#!/usr/bin/env python3
"""Remove only verified inactive resources of this practice; never wildcard IPC."""
import argparse
import fcntl
import os
from pathlib import Path
import stat
import subprocess
import sys


def lock(path):
    fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW | os.O_CLOEXEC, 0o660)
    info = os.fstat(fd)
    if not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid():
        os.close(fd)
        raise RuntimeError('Archivo de bloqueo ajeno o invalido')
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BaseException:
        os.close(fd)
        raise
    return fd


def shm_info(shmid):
    lines = Path('/proc/sysvipc/shm').read_text().splitlines()
    names = lines[0].split()
    for line in lines[1:]:
        row = dict(zip(names, line.split()))
        if int(row['shmid']) == shmid:
            return row
    raise RuntimeError('SHMID inexistente; no hay nada que eliminar')


def process_live(pid):
    try:
        text = Path(f'/proc/{pid}/stat').read_text()
    except FileNotFoundError:
        return False
    return text[text.rfind(')')+2:].split()[0] not in ('Z', 'X')


def clean_shm(shmid, key, lock_path):
    fd = lock(lock_path)
    try:
        first = shm_info(shmid)
        if int(first['key']) != key or int(first['uid']) != os.geteuid():
            raise RuntimeError('El segmento no coincide con la clave/propietario esperados')
        if int(first['nattch']) != 0 or process_live(int(first['cpid'])):
            raise RuntimeError('Segmento activo o creador vivo: limpieza rechazada')
        second = shm_info(shmid)
        if second != first:
            raise RuntimeError('El segmento cambio durante la comprobacion')
        subprocess.run(['ipcrm', '-m', str(shmid)], check=True)
        print(f'Segmento residual {shmid} eliminado con ipcrm.')
    finally:
        os.close(fd)


def clean_fifo(directory):
    fd = lock(directory / 'so_p1_fifo.lock')
    try:
        for name in ('so_p1_req.fifo', 'so_p1_resp.fifo'):
            path = directory / name
            try:
                info = path.lstat()
            except FileNotFoundError:
                continue
            if not stat.S_ISFIFO(info.st_mode) or info.st_uid != os.geteuid():
                raise RuntimeError(f'Recurso ajeno o no FIFO: {path}')
            # Even with no server lock, another process may still hold an endpoint.
            target = (info.st_dev, info.st_ino)
            for process in Path('/proc').iterdir():
                if not process.name.isdecimal():
                    continue
                try:
                    descriptors = list((process / 'fd').iterdir())
                except FileNotFoundError:
                    continue
                except PermissionError:
                    # Cannot prove inactivity if a same-UID process is hidden.
                    if process.stat().st_uid == os.geteuid():
                        raise RuntimeError('No se pueden verificar todos los descriptores del usuario')
                    continue
                for descriptor in descriptors:
                    try:
                        current = descriptor.stat()
                    except FileNotFoundError:
                        continue
                    except PermissionError:
                        raise RuntimeError(f'Descriptor no inspeccionable: {descriptor}')
                    if (current.st_dev, current.st_ino) == target:
                        raise RuntimeError(f'FIFO abierta por un proceso: {path}')
            current = path.lstat()
            if (current.st_dev, current.st_ino) != target:
                raise RuntimeError('La ruta fue reemplazada durante la comprobacion')
            path.unlink()
            print(f'FIFO residual eliminada: {path}')
    finally:
        os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    choice = parser.add_mutually_exclusive_group(required=True)
    choice.add_argument('--shmid', type=int)
    choice.add_argument('--fifo', action='store_true')
    parser.add_argument('--key', type=lambda text: int(text, 0), default=0x503141)
    parser.add_argument('--client-lock', type=Path, default=Path('/tmp/so_p1_shm_client.lock'))
    parser.add_argument('--fifo-directory', type=Path, default=Path('/tmp'))
    args = parser.parse_args()
    try:
        if args.fifo:
            clean_fifo(args.fifo_directory)
        else:
            clean_shm(args.shmid, args.key, args.client_lock)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f'Limpieza rechazada: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
