import pty
import tempfile
import os
import subprocess
import time

def test_startup_and_quit():
    # Create a temporary file for the log
    with tempfile.NamedTemporaryFile(delete=False) as log_file:
        log_path = log_file.name

    master_fd, slave_fd = None, None
    proc = None

    try:
        # Build path to the executable
        exe_path = os.path.join(os.environ.get('MESON_BUILD_ROOT', '.'), 'turbostar')
        if not os.path.exists(exe_path):
            exe_path = './turbostar'

        # Create a pseudoterminal so ncurses works correctly
        master_fd, slave_fd = pty.openpty()

        env = os.environ.copy()
        if 'TERM' not in env:
            env['TERM'] = 'xterm-256color'

        # Spawn the application attached to the pty slave
        proc = subprocess.Popen(
            [exe_path, '--log', log_path],
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            env=env
        )
        
        # We don't need the slave fd in the parent process
        os.close(slave_fd)
        slave_fd = None
        
        # Give ncurses and the UI a moment to initialize
        time.sleep(0.5)
        
        # Send the 'q' key to the master fd to tell the app to quit
        os.write(master_fd, b'q')
        
        # Wait for the process to exit
        proc.wait(timeout=2)

        # Read the log file and verify events
        with open(log_path, 'r') as f:
            log_contents = f.read()

        assert "Application started." in log_contents
        assert "UI initialized." in log_contents
        assert "Dispatching quit event." in log_contents
        assert "Exiting application loop." in log_contents

    finally:
        # Cleanup file descriptors and process
        if slave_fd is not None:
            os.close(slave_fd)
        if master_fd is not None:
            os.close(master_fd)
        if proc is not None and proc.poll() is None:
            proc.terminate()
            proc.wait()

        # Cleanup log file
        if os.path.exists(log_path):
            os.remove(log_path)

if __name__ == "__main__":
    test_startup_and_quit()
