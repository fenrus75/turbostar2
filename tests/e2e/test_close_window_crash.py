import time
from turbostar_runner import *

def test_close_window_crash():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # 1. Open a second window
        runner.send_keys('\x1bf') # Alt-F
        runner.assert_menu_active(timeout=2.0)
        runner.send_keys('n') # New window
        time.sleep(0.5)
        
        # 2. Close the active window (window 1) via Window -> Close menu
        runner.send_keys('\x1bw') # Alt-W
        runner.assert_menu_active(timeout=2.0)
        runner.send_keys('\n')    # Enter on "Close"
        time.sleep(0.5)
        
        # 3. Verify we switched back to window 0 and did not crash
        # When a window is closed, next_idx is activated, which logs "Selecting window: 0"
        runner.assert_in_log("Selecting window: 0", timeout=2.0)
        
        # 4. Type some text to ensure the editor is still fully functional and did not crash
        runner.send_keys("Still alive")
        runner.assert_text_on_screen("Still alive", timeout=2.0)
        
    except Exception as e:
        print("TEST FAILED! Log contents:")
        print(runner.get_log())
        raise e
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_close_window_crash()
    print("test_close_window_crash passed!")
