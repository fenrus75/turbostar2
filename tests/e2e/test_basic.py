import pexpect
import tempfile
import os
import subprocess

def test_startup_and_quit():
    # Create a temporary file for the log
    with tempfile.NamedTemporaryFile(delete=False) as log_file:
        log_path = log_file.name

    try:
        # Build path to the executable
        # Assuming tests are run from the build directory
        exe_path = os.path.join(os.environ.get('MESON_BUILD_ROOT', '.'), 'turbostar')
        if not os.path.exists(exe_path):
            # Fallback if MESON_BUILD_ROOT isn't quite right
            exe_path = './turbostar'

        # Spawn the application
        child = pexpect.spawn(f'{exe_path} --log {log_path}')
        
        # Wait a moment for UI to initialize
        child.expect('Turbostar Skeleton', timeout=2)
        
        # Send the 'q' key to quit
        child.send('q')
        
        # Wait for the process to exit
        child.expect(pexpect.EOF, timeout=2)

        # Read the log file and verify events
        with open(log_path, 'r') as f:
            log_contents = f.read()

        assert "Application started." in log_contents
        assert "UI initialized." in log_contents
        assert "Key pressed: 113" in log_contents # 113 is ASCII for 'q'
        assert "Exiting application loop." in log_contents

    finally:
        # Cleanup
        if os.path.exists(log_path):
            os.remove(log_path)
