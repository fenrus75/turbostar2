from turbostar_runner import *
import time
import os
import tempfile
import shutil

def test_disk_change_reload():
    runner = TurbostarRunner()
    
    # 1. Create a temp directory and file with initial content
    temp_dir = tempfile.mkdtemp(prefix="turbostar_e2e_disk_change_")
    test_file = os.path.join(temp_dir, "test_disk_change_file.txt")
    with open(test_file, 'w') as f:
        f.write("Initial line 1\n")
        
    try:
        # 2. Start editor with this file
        runner.start(filename=test_file)
        
        # Verify initial content is shown
        runner.assert_text_on_screen("Initial line 1", timeout=2.0)
        
        # 3. Wait a bit to ensure mtime changes significantly
        time.sleep(1.5)
        
        # 4. Modify file externally on disk
        with open(test_file, 'w') as f:
            f.write("External modification line\n")
            
        # 5. Wait for the 10-second check rate limit to expire
        # We sleep for 11 seconds to guarantee the tick triggers
        time.sleep(11.0)
        
        # 6. Verify that the "File Changed" reload prompt is shown
        runner.assert_text_on_screen("File Changed", timeout=2.0)
        runner.assert_text_on_screen("has changed on disk. Reload?", timeout=2.0)
        
        # 7. Press 'y' (or Enter/Yes button) to accept reloading
        runner.send_keys('y')
        time.sleep(0.5)
        
        # 8. Verify the new content is now displayed on the screen
        runner.assert_text_on_screen("External modification line", timeout=2.0)
        runner.assert_text_not_on_screen("Initial line 1", timeout=2.0)
        
        # 9. Exit cleanly
        runner.send_ctrlk('q')
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()
        if os.path.exists(temp_dir):
            try:
                shutil.rmtree(temp_dir)
            except:
                pass

def test_disk_change_never():
    runner = TurbostarRunner()
    
    # 1. Create a temp directory and file with initial content
    temp_dir = tempfile.mkdtemp(prefix="turbostar_e2e_disk_never_")
    test_file = os.path.join(temp_dir, "test_disk_change_file.txt")
    with open(test_file, 'w') as f:
        f.write("Initial line 1\n")
        
    try:
        # 2. Start editor with this file
        runner.start(filename=test_file)
        
        # Verify initial content is shown
        runner.assert_text_on_screen("Initial line 1", timeout=2.0)
        
        # 3. Wait a bit to ensure mtime changes significantly
        time.sleep(1.5)
        
        # 4. Modify file externally on disk
        with open(test_file, 'w') as f:
            f.write("External modification 1\n")
            
        # 5. Wait for the check to trigger (11 seconds)
        time.sleep(11.0)
        
        # 6. Verify that the "File Changed" reload prompt is shown
        runner.assert_text_on_screen("File Changed", timeout=2.0)
        
        # 7. Press Alt+v (Never) to ignore all future changes
        runner.send_keys(KEY_ESC + 'v')
        time.sleep(0.5)
        
        # Verify reload prompt is closed
        runner.assert_text_not_on_screen("File Changed", timeout=2.0)
        
        # 8. Wait a bit, then modify the file again on disk
        time.sleep(1.5)
        with open(test_file, 'w') as f:
            f.write("External modification 2\n")
            
        # 9. Wait for another 11 seconds
        time.sleep(11.0)
        
        # 10. Verify that the reload prompt is NOT shown
        runner.assert_text_not_on_screen("File Changed", timeout=2.0)
        
        # 11. Exit cleanly
        runner.send_ctrlk('q')
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()
        if os.path.exists(temp_dir):
            try:
                shutil.rmtree(temp_dir)
            except:
                pass

if __name__ == "__main__":
    test_disk_change_reload()
    test_disk_change_never()
    print("test_disk_change passed!")
