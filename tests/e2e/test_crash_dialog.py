import os
import time
import tempfile
import shutil
from turbostar_runner import *

def test_crash_dialog_ignore():
    # 1. Create a temporary home directory
    temp_home = tempfile.mkdtemp(prefix="test_crash_home_")
    
    # 2. Write a mock non-empty crash file
    cache_dir = os.path.join(temp_home, ".cache", "turbostar")
    crash_dir = os.path.join(cache_dir, "crashes")
    os.makedirs(crash_dir, exist_ok=True)
    crash_file = os.path.join(crash_dir, "crash_123456")
    
    crash_content = (
        "*** Turbostar Fallback Crash Catcher ***\n"
        "Caught signal: 11 (SIGSEGV - Segmentation Fault)\n\n"
        "Stack trace:\n"
        "  #0 0x55945c706285 in main + 0x13dd\n"
    )
    with open(crash_file, "w") as f:
        f.write(crash_content)
        
    runner = TurbostarRunner()
    
    # Set the cache directory override so the editor uses this exact path in the testsuite
    os.environ['TURBOSTAR_CACHE_DIR'] = cache_dir
    
    try:
        # 3. Start runner with our prepared home directory
        runner.start(home_dir=temp_home)
        time.sleep(0.5)
        
        # 4. Assert crash dialog is shown with expected title and buttons
        runner.assert_text_on_screen("Oops, you did something we did not think of", timeout=5.0)
        runner.assert_text_on_screen("Turbostar crashed in a previous run.", timeout=2.0)
        runner.assert_text_on_screen("SIGSEGV - Segmentation Fault", timeout=2.0)
        runner.assert_text_on_screen("Copy Stack Trace", timeout=2.0)
        runner.assert_text_on_screen("Ignore", timeout=2.0)
        
        # 5. Dismiss with ESC (Ignore)
        runner.send_keys(KEY_ESC)
        time.sleep(0.5)
        
        # 6. Verify dialog is closed
        runner.assert_text_not_on_screen("Oops, you did something we did not think of", timeout=3.0)
        
        # 7. Verify crash file was moved to crashes.old
        old_crash_dir = os.path.join(cache_dir, "crashes.old")
        assert os.path.exists(old_crash_dir), "crashes.old directory should exist"
        
        old_files = os.listdir(old_crash_dir)
        assert len(old_files) == 1, f"Should have exactly 1 crash file in crashes.old, got {old_files}"
        
        # Original file should be gone
        assert not os.path.exists(crash_file), "Original crash file should be removed"
        
    finally:
        if 'TURBOSTAR_CACHE_DIR' in os.environ:
            del os.environ['TURBOSTAR_CACHE_DIR']
        runner.cleanup()
        shutil.rmtree(temp_home, ignore_errors=True)

def test_crash_dialog_copy():
    # 1. Create a temporary home directory
    temp_home = tempfile.mkdtemp(prefix="test_crash_home_")
    
    # 2. Write a mock non-empty crash file
    cache_dir = os.path.join(temp_home, ".cache", "turbostar")
    crash_dir = os.path.join(cache_dir, "crashes")
    os.makedirs(crash_dir, exist_ok=True)
    crash_file = os.path.join(crash_dir, "crash_123456")
    
    crash_content = (
        "*** Turbostar Fallback Crash Catcher ***\n"
        "Caught signal: 11 (SIGSEGV - Segmentation Fault)\n\n"
        "Stack trace:\n"
        "  #0 0x55945c706285 in main + 0x13dd\n"
    )
    with open(crash_file, "w") as f:
        f.write(crash_content)
        
    runner = TurbostarRunner()
    
    # Set the cache directory override
    os.environ['TURBOSTAR_CACHE_DIR'] = cache_dir
    
    try:
        # 3. Start runner
        runner.start(home_dir=temp_home)
        time.sleep(0.5)
        
        # 4. Assert crash dialog is shown
        runner.assert_text_on_screen("Oops, you did something we did not think of", timeout=5.0)
        
        # 5. Move focus from 'Ignore' (default) to 'Copy Stack Trace' using Tab
        runner.send_keys("\t")
        time.sleep(0.1)
        # Press Enter to click the Copy button
        runner.send_keys("\n")
        time.sleep(0.5)
        
        # 6. Verify dialog is closed
        runner.assert_text_not_on_screen("Oops, you did something we did not think of", timeout=3.0)
        
        # 7. Verify crash file was moved to crashes.old
        old_crash_dir = os.path.join(cache_dir, "crashes.old")
        assert os.path.exists(old_crash_dir), "crashes.old directory should exist"
        
        old_files = os.listdir(old_crash_dir)
        assert len(old_files) == 1, "Should have exactly 1 crash file in crashes.old"
        
        # Original file should be gone
        assert not os.path.exists(crash_file), "Original crash file should be removed"
        
    finally:
        if 'TURBOSTAR_CACHE_DIR' in os.environ:
            del os.environ['TURBOSTAR_CACHE_DIR']
        runner.cleanup()
        shutil.rmtree(temp_home, ignore_errors=True)

if __name__ == "__main__":
    test_crash_dialog_ignore()
    test_crash_dialog_copy()
    print("All crash dialog tests passed!")
