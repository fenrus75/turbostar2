import time
import os
import tempfile
import shutil
from turbostar_runner import *

def test_mouse_scroll_zorder():
    runner = TurbostarRunner()
    
    # Create temp files
    temp_dir = tempfile.mkdtemp(dir=os.getcwd())
    file1_path = os.path.join(temp_dir, "file1.txt")
    file2_path = os.path.join(temp_dir, "file2.txt")
    
    # Write 25 lines to file1
    with open(file1_path, "w") as f:
        f.write("".join(f"X{i}\n" for i in range(1, 26)))
        
    # Write 25 lines to file2
    with open(file2_path, "w") as f:
        f.write("".join(f"A{i}\n" for i in range(1, 26)))
        
    try:
        # Start editor opening both files. File2 will be the active window (=2=) on top
        runner.start(filename=f"{file1_path} {file2_path}")
        
        # Verify second window is active and has '=2=' in the title bar
        runner.assert_text_on_screen("=2=", timeout=2.0)
        
        # Verify active file content
        runner.assert_text_on_screen("A1 ", timeout=2.0)
        runner.assert_text_not_on_screen("A23", timeout=2.0)
        
        # Send mouse scroll down events at content area (x=15, y=5)
        # SGR mouse wheel down button = 65
        # Coordinate x=15, y=5 translates to: 1-based x=16, y=6
        # Scroll down 8 times (8 * 3 = 24 lines) to push viewport down so A23 becomes visible
        for _ in range(8):
            runner.send_raw_keys(b"\x1b[<65;16;6M")
            time.sleep(0.05)
        
        # Scroll down 8 times = 24 lines.
        # "A1" was at the top. Since viewport scrolled down past the screen height,
        # "A1" should be scrolled out of view.
        # "A23" (line 23) should now be visible on screen.
        runner.assert_text_not_on_screen("A1 ", timeout=2.0)
        runner.assert_text_on_screen("A23", timeout=2.0)
        
    except Exception as e:
        if os.path.exists(file2_path):
            with open(file2_path, 'r') as f:
                print("--- ACTUAL FILE2.TXT ON DISK ---")
                print(f.read())
        if hasattr(runner, 'log_path') and os.path.exists(runner.log_path):
            with open(runner.log_path, 'r') as f:
                print("--- FULL EDITOR LOG ---")
                print(f.read())
        raise e
    finally:
        runner.cleanup()
        shutil.rmtree(temp_dir)

if __name__ == "__main__":
    test_mouse_scroll_zorder()
    print("test_mouse_scroll_zorder passed!")
