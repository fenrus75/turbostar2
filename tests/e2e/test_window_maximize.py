import time
import os
import tempfile
import shutil
from turbostar_runner import *

def test_window_maximize():
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
        time.sleep(0.2)
        
        # Mouse drag to (60, 16) -> SGR: button 32, x=61, y=17
        runner.send_raw_keys(b"\x1b[<32;61;17M")
        time.sleep(0.2)
        
        # Mouse drag to (40, 10) -> SGR: button 32, x=41, y=11
        runner.send_raw_keys(b"\x1b[<32;41;11M")
        time.sleep(0.2)
        
        # Mouse release at (40, 10) -> SGR: button 0 release, x=41, y=11
        runner.send_raw_keys(b"\x1b[<0;41;11m")
        time.sleep(0.5)

        # 2. Drag the title bar of the smaller window to move it to x=30, y=5!
        # Title bar starts at row y=1. Drag from (20, 1) to (30, 5) to avoid Git/Close buttons.
        
        # Mouse down at (20, 1) -> SGR: button 0, x=21, y=2
        runner.send_raw_keys(b"\x1b[<0;21;2M")
        time.sleep(0.2)
        
        # Mouse drag to (25, 3) -> SGR: button 32, x=26, y=4
        runner.send_raw_keys(b"\x1b[<32;26;4M")
        time.sleep(0.2)
        
        # Mouse drag to (30, 5) -> SGR: button 32, x=31, y=6
        runner.send_raw_keys(b"\x1b[<32;31;6M")
        time.sleep(0.2)
        
        # Mouse release at (30, 5) -> SGR: button 0 release, x=31, y=6
        runner.send_raw_keys(b"\x1b[<0;31;6m")
        time.sleep(0.5)

        # 3. Double-click the title bar of this smaller window.
        # Title bar is at row y=5, x ranges from 30 to 69.
        # Let's click at x=35, y=5.
        runner.send_mouse_click(35, 5)
        time.sleep(0.1)
        runner.send_mouse_click(35, 5)
        time.sleep(0.5)

        # Assert log says double click detected
        runner.assert_in_log("Double click detected on title bar. Maximizing/restoring window.", timeout=2.0)

        # 4. Double click again to restore
        # Now the window is maximized: title bar is at y=1, x ranges from 0 to 79.
        # Click at x=35, y=1 (outside Git status button).
        runner.send_mouse_click(35, 1)
        time.sleep(0.1)
        runner.send_mouse_click(35, 1)
        time.sleep(0.5)

        # Assert log shows double click detected twice
        runner.assert_in_log("Double click detected on title bar. Maximizing/restoring window.", timeout=2.0, count=2)

        # 5. Test Maximize via Window Popup Menu
        # Open popup menu with Alt+= (Esc + =)
        runner.send_keys("\x1b=")
        time.sleep(0.1)
        runner.assert_text_on_screen("Maximize", timeout=2.0)

        # Select Maximize (hotkey 'M')
        runner.send_keys("m")
        time.sleep(0.5)
        runner.assert_in_log("Dispatching maximize_window event.", timeout=2.0, count=1)

        # 6. Test Restore via Window Popup Menu
        # Open popup menu with Alt+= again
        runner.send_keys("\x1b=")
        time.sleep(0.1)
        runner.assert_text_on_screen("Restore", timeout=2.0)

        # Select Restore (hotkey 'R')
        runner.send_keys("r")
        time.sleep(0.5)
        runner.assert_in_log("Dispatching maximize_window event.", timeout=2.0, count=2)

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
    test_window_maximize()
    print("test_window_maximize passed!")
