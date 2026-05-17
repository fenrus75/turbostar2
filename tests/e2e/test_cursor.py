import time
import os
import tempfile
from turbostar_runner import TurbostarRunner

def test_cursor_movement():
    # Create a temporary file with content
    with tempfile.NamedTemporaryFile(mode='w', delete=False) as f:
        f.write("Line 1\nLine 2\nLine 3")
        test_file = f.name

    runner = TurbostarRunner()
    try:
        runner.start(filename=test_file)
        
        # Verify initial cursor position
        runner.assert_cursor_position(1, 1)
        
        # Move Down
        runner.send_keys('\x1b[B')
        runner.assert_cursor_position(2, 1)
        
        # Move Right
        runner.send_keys('\x1b[C')
        runner.assert_cursor_position(2, 2)
        
        # Move Right
        runner.send_keys('\x1b[C')
        runner.assert_cursor_position(2, 3)

        # Move Up
        runner.send_keys('\x1b[A')
        runner.assert_cursor_position(1, 3)
        
        # Quit
        runner.send_ctrlk('q')
        runner.wait(timeout=5)
    except Exception as e:
        print(f"FAILED. Log contents:\n{runner.get_log()}")
        raise e
    finally:
        runner.cleanup()
        if os.path.exists(test_file):
            os.remove(test_file)

if __name__ == "__main__":
    test_cursor_movement()
