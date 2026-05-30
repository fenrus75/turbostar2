import time
import os
import tempfile
import shutil
from turbostar_runner import *

def test_window_drag_resize():
    runner = TurbostarRunner()
    
    # Create a temp file
    temp_dir = tempfile.mkdtemp(dir=os.getcwd())
    file_path = os.path.join(temp_dir, "test.txt")
    with open(file_path, "w") as f:
        f.write("Line 1 of content\nLine 2 of content\nLine 3 of content\n")
        
    try:
        # Start editor
        runner.start(filename=file_path)
        runner.assert_text_on_screen("=1=", timeout=2.0)
        runner.assert_text_on_screen("Line 1 of content")

        # 1. Resize the window to make it smaller (from 80x22 to 40x10)
        # Drag the bottom-right corner at x=79, y=22 (0-based) to x=40, y=10.
        
        # Mouse down at (79, 22) -> SGR: button 0, x=80, y=23
        runner.send_raw_keys(b"\x1b[<0;80;23M")
        time.sleep(0.05)
        
        # Mouse drag to (60, 16) -> SGR: button 32, x=61, y=17
        runner.send_raw_keys(b"\x1b[<32;61;17M")
        time.sleep(0.05)
        
        # Mouse drag to (40, 10) -> SGR: button 32, x=41, y=11
        runner.send_raw_keys(b"\x1b[<32;41;11M")
        time.sleep(0.05)
        
        # Mouse release at (40, 10) -> SGR: button 0 release, x=41, y=11
        runner.send_raw_keys(b"\x1b[<0;41;11m")
        time.sleep(0.1)

        # 2. Drag the title bar of the smaller window to move it to x=20, y=5!
        # Title bar is at row y=1. Drag from (10, 1) to (20, 5).
        
        # Mouse down at (10, 1) -> SGR: button 0, x=11, y=2
        runner.send_raw_keys(b"\x1b[<0;11;2M")
        time.sleep(0.05)
        
        # Mouse drag to (15, 3) -> SGR: button 32, x=16, y=4
        runner.send_raw_keys(b"\x1b[<32;16;4M")
        time.sleep(0.05)
        
        # Mouse drag to (20, 5) -> SGR: button 32, x=21, y=6
        runner.send_raw_keys(b"\x1b[<32;21;6M")
        time.sleep(0.05)
        
        # Mouse release at (20, 5) -> SGR: button 0 release, x=21, y=6
        runner.send_raw_keys(b"\x1b[<0;21;6m")
        time.sleep(0.2)
        
    except Exception as e:
        if hasattr(runner, 'log_path') and os.path.exists(runner.log_path):
            with open(runner.log_path, 'r') as f:
                print("--- FULL EDITOR LOG ---")
                print(f.read())
        raise e
    finally:
        runner.cleanup()
        shutil.rmtree(temp_dir)

if __name__ == "__main__":
    test_window_drag_resize()
    print("test_window_drag_resize passed!")
