import pty
import tempfile
import os
import subprocess
import time
import select
import pyte

class TurbostarRunner:
    def __init__(self, cols=80, lines=24):
        with tempfile.NamedTemporaryFile(delete=False) as log_file:
            self.log_path = log_file.name
        self.master_fd = None
        self.slave_fd = None
        self.proc = None
        self.cols = cols
        self.lines = lines
        self.screen = pyte.Screen(cols, lines)
        self.stream = pyte.Stream(self.screen)

    def start(self, filename=None):
        exe_path = os.path.join(os.environ.get('MESON_BUILD_ROOT', '.'), 'turbostar')
        if not os.path.exists(exe_path):
            exe_path = './turbostar'

        cmd = [exe_path, '--log', self.log_path]
        if filename:
            cmd.append(filename)

        self.master_fd, self.slave_fd = pty.openpty()

        env = os.environ.copy()
        if 'TERM' not in env:
            env['TERM'] = 'xterm-256color'
        env['COLUMNS'] = str(self.cols)
        env['LINES'] = str(self.lines)

        self.proc = subprocess.Popen(
            cmd,
            stdin=self.slave_fd,
            stdout=self.slave_fd,
            stderr=self.slave_fd,
            env=env
        )
        os.close(self.slave_fd)
        self.slave_fd = None
        
        # Give ncurses and the UI a moment to initialize
        time.sleep(0.5)
        self._read_output()

    def _read_output(self):
        if self.master_fd is None:
            return
        while True:
            r, _, _ = select.select([self.master_fd], [], [], 0)
            if not r:
                break
            try:
                data = os.read(self.master_fd, 4096)
                if not data:
                    break
                self.stream.feed(data.decode('utf-8', errors='replace'))
            except OSError:
                break

    def send_keys(self, keys):
        if isinstance(keys, str):
            # Convert \n to \r because we use nonl() in the editor
            keys = keys.replace('\n', '\r')
        self.send_raw_keys(keys)

    def send_raw_keys(self, keys):
        if isinstance(keys, str):
            keys = keys.encode('utf-8')
        time.sleep(0.2)
        os.write(self.master_fd, keys)
        time.sleep(0.2)
        self._read_output()

    def wait(self, timeout=2):
        if self.proc:
            self.proc.wait(timeout=timeout)
        self._read_output()

    def get_log(self):
        if os.path.exists(self.log_path):
            with open(self.log_path, 'r') as f:
                return f.read()
        return ""

    def get_cursor_position(self):
        self._read_output()
        # The cursor position is in the status bar at the bottom, row = self.lines - 1
        # It starts at index 1 (after a space)
        # We need to extract the string "Y:X" from row[1:]
        status_bar_row = self.screen.display[self.lines - 1]
        # Look for the part like " Y:X "
        import re
        match = re.search(r"(\d+):(\d+)", status_bar_row)
        if match:
            return int(match.group(1)), int(match.group(2))
        return -1, -1

    def assert_cursor_position(self, expected_y, expected_x, timeout=1.0):
        start_time = time.time()
        while time.time() - start_time < timeout:
            actual_y, actual_x = self.get_cursor_position()
            if (actual_y, actual_x) == (expected_y, expected_x):
                return
            time.sleep(0.1)
        
        actual_y, actual_x = self.get_cursor_position()
        raise AssertionError(f"Cursor position mismatch! Expected ({expected_y}, {expected_x}), got ({actual_y}, {actual_x}) after timeout")

    def assert_text_on_screen(self, text):
        self._read_output()
        for line in self.screen.display:
            if text in line:
                return
        
        display_str = "\n".join(self.screen.display)
        raise AssertionError(f"Text '{text}' not found on screen. Screen content:\n{display_str}")

    def cleanup(self):
        if self.slave_fd is not None:
            os.close(self.slave_fd)
            self.slave_fd = None
        if self.master_fd is not None:
            # Try to quit gracefully via ^KQ
            try:
                self.send_keys('\x0b' + 'q')
                time.sleep(0.2)
            except:
                pass
            os.close(self.master_fd)
            self.master_fd = None
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            self.proc.wait()
            self.proc = None
        if os.path.exists(self.log_path):
            os.remove(self.log_path)
