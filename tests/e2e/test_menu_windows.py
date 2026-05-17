import time
from turbostar_runner import TurbostarRunner

def test_window_menu_switching():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Open a second window
        runner.send_keys('\x1bf') # Alt-F
        runner.assert_menu_active(timeout=2.0)
        runner.send_keys('n') # New window
        
        # 2. Check "Window" menu content
        runner.send_keys('\x1bw') # Alt-W
        runner.assert_menu_active(timeout=2.0)
        
        # 3. Select the second item (index 1)
        # Use Down key twice then Enter
        runner.send_keys('\x1b[B', count=2) # Down x 2
        runner.send_keys('\n')
        
        # 4. Verify log for correct window selection
        # Note: activate_window logs "Selecting window: N" via dispatcher
        runner.assert_in_log("Selecting window: 1", timeout=2.0)

        # 5. Select the first window item (index 0)
        runner.send_keys('\x1bw') # Alt-W
        runner.assert_menu_active(timeout=2.0)
        # Menu starts at "Close". Window 0 is one Down away (skipping separator).
        runner.send_keys('\x1b[B') # Down x 1
        runner.send_keys('\n')
        
        runner.assert_in_log("Selecting window: 0", timeout=2.0)

        runner.send_ctrlk('q')
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_window_menu_switching()
    print("test_window_menu_switching passed!")