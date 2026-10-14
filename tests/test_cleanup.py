"""Safe FIFO cleanup: private paths only, no production IPC."""
import fcntl
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/cleanup_ipc.py'


class CleanupTests(unittest.TestCase):
    def test_inactive_active_and_foreign_paths(self):
        with tempfile.TemporaryDirectory(prefix='so-cleanup-') as directory:
            root=Path(directory)
            req=root/'so_p1_req.fifo'; resp=root/'so_p1_resp.fifo'
            lock=root/'so_p1_fifo.lock'
            command=['python3',str(SCRIPT),'--fifo','--fifo-directory',str(root)]
            def run(code):
                result=subprocess.run(command,capture_output=True,timeout=3)
                self.assertEqual(result.returncode,code,result.stderr)
            os.mkfifo(req); os.mkfifo(resp)
            with lock.open('w') as file:
                fcntl.flock(file,fcntl.LOCK_EX|fcntl.LOCK_NB)
                run(1)
                self.assertTrue(req.exists()); self.assertTrue(resp.exists())
            inode=lock.stat().st_ino
            endpoint=os.open(req,os.O_RDONLY|os.O_NONBLOCK)
            try:
                run(1); self.assertTrue(req.exists())
            finally: os.close(endpoint)
            result=subprocess.run(command,capture_output=True,timeout=3)
            if result.returncode:
                # WSL can hide descriptors of another same-UID session. Refusal
                # is mandatory when inactivity cannot be proven, even in a test.
                self.assertIn(b'inspeccionable',result.stderr)
                self.assertTrue(req.exists()); self.assertTrue(resp.exists())
                print('INFO: /proc restringido; verificada negativa conservadora de limpieza FIFO.')
                req.unlink(); resp.unlink()  # Only this test's private known nodes.
            else:
                self.assertFalse(req.exists()); self.assertFalse(resp.exists())
            self.assertFalse(req.exists()); self.assertFalse(resp.exists())
            self.assertEqual(lock.stat().st_ino,inode)
            req.write_text('preserve regular file')
            run(1); self.assertEqual(req.read_text(),'preserve regular file')
            req.unlink()
            target=root/'target'; target.write_text('preserve symlink target')
            req.symlink_to(target)
            run(1); self.assertTrue(req.is_symlink())
            self.assertEqual(target.read_text(),'preserve symlink target')


if __name__=='__main__':
    unittest.main(verbosity=2)
