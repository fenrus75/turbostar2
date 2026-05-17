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
        
        self.cols = cols
        self.lines = lines
        self.screen = pyte.Screen(cols, lines)
        self.stream = pyte.Stream(self.screen)
        self.proc = None
        self.master_fd = None
        self.slave_fd = None

    def start(self, filename=None, use_lsp=False):
        project_root = os.environ.get('PROJECT_ROOT', os.getcwd())
        testrun_dir = os.path.join(project_root, 'testrun')
        os.makedirs(testrun_dir, exist_ok=True)

        # Create a unique temporary directory for this test's HOME
        self.temp_home = tempfile.mkdtemp(prefix="turbostar_test_home_")

        # Binary is now automatically copied to testrun/ by the build system
        exe_path = './turbostar'
        log_path_abs = os.path.abspath(self.log_path)

        cmd = [exe_path, '--log', log_path_abs]
        if not use_lsp:
            cmd.append('--no-lsp')

        if filename:
            # Handle multiple filenames if provided as space-separated string
            cmd.extend(filename.split())

        self.master_fd, self.slave_fd = pty.openpty()

        env = os.environ.copy()
        if 'TERM' not in env:
            env['TERM'] = 'xterm-256color'
        env['COLUMNS'] = str(self.cols)
        env['LINES'] = str(self.lines)
        env['HOME'] = self.temp_home # Isolate config/history

        self.proc = subprocess.Popen(
            cmd,
            stdin=self.slave_fd,
            stdout=self.slave_fd,
            stderr=self.slave_fd,
            env=env,
            cwd=testrun_dir
        )
        os.close(self.slave_fd)
        self.slave_fd = None
        
        # Wait for the UI to initialize
        self.assert_in_log("UI initialized.", timeout=5.0)

    def _read_output(self):
        if self.master_fd is None:
            return
        
        while True:
            r, _, _ = select.select([self.master_fd], [], [], 0.01)
            if not r:
                break
            try:
                data = os.read(self.master_fd, 1024)
                if not data:
                    break
                self.stream.feed(data.decode('utf-8', errors='replace'))
            except OSError:
                break

    def send_keys(self, keys, count=1):
        if isinstance(keys, str):
            # Convert \n to \r because we use nonl() in the editor
            keys = keys.replace('\n', '\r')
        for _ in range(count):
            self.send_raw_keys(keys)

    def send_raw_keys(self, keys):
        if isinstance(keys, str):
            keys = keys.encode('utf-8')
        for char_byte in keys:
            time.sleep(0.01)
            os.write(self.master_fd, bytes([char_byte]))
            time.sleep(0.01)
        self._read_output()

    def send_ctrlk(self, cmd_char):
        self.send_raw_keys(b'\x0b')
        # Wait for the K-block prompt to appear on the status bar to ensure editor readiness
        self.assert_text_on_screen("K-Block:", timeout=2.0)
        self.send_raw_keys(cmd_char.encode('utf-8'))

    def send_mouse_click(self, x, y):
        """Send a left mouse click at the given 0-based terminal coordinates."""
        seq_down = f"\x1b[<0;{x+1};{y+1}M"
        seq_up   = f"\x1b[<0;{x+1};{y+1}m"
        self.send_keys(seq_down)
        self.send_keys(seq_up)

    def insert_file(self, rel_path):
        project_root = os.environ.get('PROJECT_ROOT', os.getcwd())
        abs_path = os.path.join(project_root, rel_path)
        self.send_ctrlk('r')
        # Wait for the Insert File dialog to appear
        self.assert_text_on_screen("Insert File", timeout=2.0)
        self.send_keys(abs_path + '\n')
        # Wait for the dialog to close and the insertion to finish
        self.assert_text_not_on_screen("Insert File", timeout=2.0)

    def wait(self, timeout=5):
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
        # The status bar is on the last row
        # We need to extract the string "Y:X" from row[1:]
        status_bar_row = self.screen.display[self.lines - 1]
        # Look for the part like " Y:X "
        import re
        match = re.search(r"(\d+):(\d+)", status_bar_row)
        if match:
            print(f"DEBUG: Status bar: '{status_bar_row}' -> {match.group(1)}:{match.group(2)}")
            return int(match.group(1)), int(match.group(2))
        return -1, -1


    def assert_in_log(self, text, timeout=1.0):
        start_time = time.time()
        while time.time() - start_time < timeout:
            log = self.get_log()
            if text in log:
                return
            time.sleep(0.1)
        raise AssertionError(f"Text '{text}' not found in log after {timeout}s")

    def assert_file_exists(self, path, timeout=1.0):
        start_time = time.time()
        while time.time() - start_time < timeout:
            if os.path.exists(path):
                return
            time.sleep(0.1)
        raise AssertionError(f"File '{path}' does not exist after {timeout}s")

    def assert_file_contains(self, path, text, timeout=1.0):
        self.assert_file_exists(path, timeout)
        start_time = time.time()
        while time.time() - start_time < timeout:
            with open(path, 'r') as f:
                content = f.read()
            if text in content:
                return
            time.sleep(0.1)
        raise AssertionError(f"File '{path}' does not contain '{text}' after {timeout}s")

    def assert_cursor_position(self, expected_y, expected_x, timeout=1.0):
        start_time = time.time()
        while time.time() - start_time < timeout:
            actual_y, actual_x = self.get_cursor_position()
            if actual_y == expected_y and actual_x == expected_x:
                return
            time.sleep(0.1)

        actual_y, actual_x = self.get_cursor_position()
        raise AssertionError(f"Cursor position mismatch! Expected ({expected_y}, {expected_x}), got ({actual_y}, {actual_x}) after timeout")

    def assert_text_on_screen(self, text, timeout=1.0):
        start_time = time.time()
        while time.time() - start_time < timeout:
            self._read_output()
            for line in self.screen.display:
                if text in line:
                    return
            time.sleep(0.1)
        
        display_str = "\n".join(self.screen.display)
        raise AssertionError(f"Text '{text}' not found on screen after {timeout}s. Screen content:\n{display_str}")

    def assert_text_not_on_screen(self, text, timeout=1.0):
        start_time = time.time()
        while time.time() - start_time < timeout:
            self._read_output()
            found = False
            for line in self.screen.display:
                if text in line:
                    found = True
                    break
            if not found:
                return
            time.sleep(0.1)

        display_str = "\n".join(self.screen.display)
        raise AssertionError(f"Text '{text}' found on screen after {timeout}s, but should not be. Screen content:\n{display_str}")

    def assert_git_status(self, expected_status, timeout=1.0):
        """
        Asserts that the git status indicator in the title bar matches the expected status.
        expected_status is typically '[✔]', '[✎]', or '[?]'.
        """
        start_time = time.time()
        while time.time() - start_time < timeout:
            self._read_output()
            # Title bar is typically on line index 1
            title_line = self.screen.display[1] if len(self.screen.display) > 1 else ""
            if expected_status in title_line:
                return
            time.sleep(0.1)
        
        title_line = self.screen.display[1] if len(self.screen.display) > 1 else ""
        raise AssertionError(f"Git status '{expected_status}' not found in title bar after {timeout}s. Title line:\n{title_line}")

    def assert_selection_is(self, start_y, start_x, end_y, end_x, timeout=1.0):
        """
        Asserts that the selection markers are at the specified positions (1-based).
        Pass None for y/x if no selection is expected (S=none E=none).
        """
        import re
        start_time = time.time()
        
        expected_s = f"{start_y}:{start_x}" if start_y is not None else "none"
        expected_e = f"{end_y}:{end_x}" if end_y is not None else "none"

        while time.time() - start_time < timeout:
            log = self.get_log()
            # Find the latest "State:" line
            matches = list(re.finditer(r"State:.*?S=(\S+).*?E=(\S+)", log))
            if matches:
                last_match = matches[-1]
                actual_s = last_match.group(1)
                actual_e = last_match.group(2)
                
                if actual_s == expected_s and actual_e == expected_e:
                    return
            time.sleep(0.1)

        log = self.get_log()
        matches = list(re.finditer(r"State:.*?S=(\S+).*?E=(\S+)", log))
        actual_state = matches[-1].group(0) if matches else "Unknown"
        raise AssertionError(f"Selection mismatch! Expected S={expected_s} E={expected_e}. Latest state: {actual_state}")

    def assert_content_is(self, reference_file_path):
        import tempfile
        import filecmp
        import difflib

        # 1. Generate a temporary path for saving
        with tempfile.NamedTemporaryFile(suffix=".txt", delete=False) as tmp:
            save_path = tmp.name

        try:
            # 2. Trigger Save As via keys (^KW)
            self.send_ctrlk('w')
            self.assert_text_on_screen("Save File As", timeout=2.0)
            # 3. Clear pre-filled and type path
            self.send_keys('\x7f', count=50)
            self.send_keys(save_path + '\n')
            self.assert_text_not_on_screen("Save File As", timeout=2.0)

            # 4. Compare files
            if not os.path.exists(save_path):
                raise AssertionError(f"Save failed during assert_content_is. {save_path} not found.")

            # If path is relative to project root, find it
            ref_path = reference_file_path
            if not os.path.isabs(ref_path):
                project_root = os.environ.get('PROJECT_ROOT', os.getcwd())
                ref_path = os.path.join(project_root, ref_path)

            if not os.path.exists(ref_path):
                raise FileNotFoundError(f"Reference file not found: {ref_path}")

            with open(save_path, 'r') as f1, open(ref_path, 'r') as f2:
                lines1 = f1.readlines()
                lines2 = f2.readlines()
                
                c1 = "".join(lines1).strip()
                c2 = "".join(lines2).strip()
                
                if c1 != c2:
                    print(f"\nContent Mismatch for {reference_file_path}:")
                    diff = difflib.unified_diff(
                        lines2, lines1, 
                        fromfile='Expected (reference)', 
                        tofile='Actual (saved)'
                    )
                    for line in diff:
                        print(line, end='')
                    print("\n")
                    assert c1 == c2
        finally:
            if os.path.exists(save_path):
                os.remove(save_path)

    def cleanup(self):
        if self.slave_fd is not None:
            os.close(self.slave_fd)
            self.slave_fd = None
        if self.master_fd is not None:
            # Try to quit gracefully via ^KQ
            try:
                os.write(self.master_fd, b'\x0b' + b'q')
                time.sleep(0.1)
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
            
        # Clean up default file to prevent state leak across tests
        project_root = os.environ.get('PROJECT_ROOT', os.getcwd())
        testrun_dir = os.path.join(project_root, 'testrun')
        default_file = os.path.join(testrun_dir, 'unknown.txt')
        if os.path.exists(default_file):
            os.remove(default_file)
            
        import shutil
        if hasattr(self, 'temp_home') and os.path.exists(self.temp_home):
            shutil.rmtree(self.temp_home)
