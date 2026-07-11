import os
import time
import tempfile
import shutil
from turbostar_runner import *

def test_image_persistence():
    temp_home = tempfile.mkdtemp(prefix="turbostar_test_image_")
    
    # Set a stable cache directory to override the PID suffix used in test suite mode
    cache_dir = os.path.join(temp_home, ".cache", "turbostar")
    os.makedirs(cache_dir, exist_ok=True)
    os.environ["TURBOSTAR_CACHE_DIR"] = cache_dir
    
    runner = TurbostarRunner()
    
    project_root = os.environ.get('PROJECT_ROOT', os.getcwd())
    logo_path = os.path.join(project_root, 'tests', 'data', 'logo.jpg')
    
    try:
        runner.start(home_dir=temp_home)
        
        # 1. Open Agent -> Image VFS Manager
        runner.send_keys(KEY_ESC + 'a')
        runner.assert_text_on_screen("Image VFS Manager...", timeout=2.0)
        runner.send_keys('i')
        
        runner.assert_text_on_screen("Image VFS Manager", timeout=2.0)
        runner.assert_text_on_screen("VFS Images:", timeout=2.0)
        
        # 2. Press Tab to focus Import button, and press Enter to click it
        runner.send_keys('\t')
        time.sleep(0.2)
        runner.send_keys('\n')
        
        runner.assert_text_on_screen("Import Image", timeout=3.0)
        
        # 3. Type path to logo.jpg and press Enter
        runner.send_keys(KEY_CTRL_Y)
        runner.send_keys(logo_path + '\n')
        
        # Verify it was imported
        runner.assert_text_not_on_screen("Import Image", timeout=3.0)
        runner.assert_text_on_screen("logo", timeout=3.0)
        
        # 4. Exit the VFS Manager dialog
        runner.send_keys(KEY_ESC)
        runner.assert_text_not_on_screen("Image VFS Manager", timeout=2.0)
        
        # 5. Exit Turbostar (Alt+F, X)
        runner.send_keys(KEY_ESC + 'f')
        runner.send_keys('x')
        runner.wait(timeout=5)
        
        # 6. Start Turbostar again
        runner = TurbostarRunner()
        runner.start(home_dir=temp_home)
        
        # 7. Open Agent -> Image VFS Manager again
        runner.send_keys(KEY_ESC + 'a')
        runner.send_keys('i')
        
        # Verify that the imported image is still present in the list
        runner.assert_text_on_screen("Image VFS Manager", timeout=2.0)
        runner.assert_text_on_screen("logo", timeout=3.0)
        
        runner.send_keys(KEY_ESC)
        
    finally:
        runner.cleanup()
        shutil.rmtree(temp_home, ignore_errors=True)

if __name__ == "__main__":
    test_image_persistence()
    print("test_image_manager E2E passed!")
