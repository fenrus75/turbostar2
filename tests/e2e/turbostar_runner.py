import pty
import tempfile
import os
import subprocess
import time

class TurbostarRunner:
    def __init__(self):
        with tempfile.NamedTemporaryFile(delete=False) as log_file:
            self.log_path = log_file.name
        self.master_fd = None
        self.slave_fd = None
        self.proc = None

    def start(self):
        exe_path = os.path.join(os.environ.get('MESON_BUILD_ROOT', '.'), 'turbostar')
        if not os.path.exists(exe_path):
            exe_path = './turbostar'

        self.master_fd, self.slave_fd = pty.openpty()

        env = os.environ.copy()
        if 'TERM' not in env:
            env['TERM'] = 'xterm-256color'

        self.proc = subprocess.Popen(
            [exe_path, '--log', self.log_path],
            stdin=self.slave_fd,
            stdout=self.slave_fd,
            stderr=self.slave_fd,
            env=env
        )
        os.close(self.slave_fd)
        self.slave_fd = None
        
        # Give ncurses and the UI a moment to initialize
        time.sleep(0.5)

    def send_keys(self, keys):
        if isinstance(keys, str):
            keys = keys.encode('utf-8')
        os.write(self.master_fd, keys)

    def wait(self, timeout=2):
        if self.proc:
            self.proc.wait(timeout=timeout)

    def get_log(self):
        if os.path.exists(self.log_path):
            with open(self.log_path, 'r') as f:
                return f.read()
        return ""

    def cleanup(self):
        if self.slave_fd is not None:
            os.close(self.slave_fd)
            self.slave_fd = None
        if self.master_fd is not None:
            os.close(self.master_fd)
            self.master_fd = None
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            self.proc.wait()
            self.proc = None
        if os.path.exists(self.log_path):
            os.remove(self.log_path)
