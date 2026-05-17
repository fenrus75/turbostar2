import time
import os
import tempfile
from turbostar_runner import TurbostarRunner

def test_cursor_wrap():
    # Create a temporary file with content
    project_root = os.environ.get('PROJECT_ROOT', os.getcwd())
    testrun_dir = os.path.join(project_root, "testrun")
    test_file = os.path.join(testrun_dir, "test_wrap.txt")
    os.makedirs(testrun_dir, exist_ok=True)
    with open(test_file, 'w') as f:
        f.write("ABC\nDEF")

    runner = TurbostarRunner()
    try:
        runner.start(filename="test_wrap.txt")
        
        # Initial: 1:1 (A)
        runner.assert_cursor_position(1, 1)
        
        # Move Right 3 times: 1:4 (after C)
        runner.send_keys('\x1b[C', count=3)
        runner.assert_cursor_position(1, 4)
        
        # Move Right once more: should wrap to 2:1 (D)
        runner.send_keys('\x1b[C')
        runner.assert_cursor_position(2, 1)
        
        # Move Left once: should wrap back to 1:4 (after C)
        runner.send_keys('\x1b[D')
        runner.assert_cursor_position(1, 4)

        # Quit
        runner.send_keys('\x0b' + 'q')
        runner.wait(timeout=5)
    except Exception as e:
        print(f"FAILED. Log contents:\n{runner.get_log()}")
        raise e
    finally:
        runner.cleanup()
        if os.path.exists(test_file):
            os.remove(test_file)

if __name__ == "__main__":
    test_cursor_wrap()
