import time
import os
import tempfile
import shutil
from turbostar_runner import TurbostarRunner

def test_save_mouse():
    runner = TurbostarRunner()
    
    # Create temp directory manually within current dir
    temp_dir = tempfile.mkdtemp(dir=os.getcwd())
    file_path = os.path.join(temp_dir, "test_save.txt")
    with open(file_path, "w") as f:
        f.write("Initial\n")
        
    try:
        runner.start(filename=file_path)
        
        runner.assert_text_on_screen("test_save.txt", timeout=2.0)
        runner.send_keys("More text")
        runner.assert_text_on_screen("test_save.txt*", timeout=2.0)
        
        # "test_save.txt*" is 14 chars.
        # width = 80
        # title_x = (80 - 14) // 2 = 33
        # star_x = 33 + 14 - 1 = 46
        
        runner.send_mouse_click(46, 1)
        
        # Wait for * to disappear
        runner.assert_text_not_on_screen("test_save.txt*", timeout=2.0)
        
        
    finally:
        runner.cleanup()
        shutil.rmtree(temp_dir)

if __name__ == "__main__":
    test_save_mouse()
    print("test_save_mouse passed!")
