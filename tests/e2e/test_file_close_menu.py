import time
from turbostar_runner import *

def test_file_close_menu():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # 1. Open a second window via File -> New File
        runner.send_keys('\x1bf') # Alt-F
        runner.assert_menu_active(timeout=2.0)
        runner.send_keys('n') # New File
        time.sleep(0.5)
        
        # 2. Close active window via File -> Close
        runner.send_keys('\x1bf') # Alt-F
        runner.assert_menu_active(timeout=2.0)
        runner.assert_text_on_screen("Close", timeout=2.0)
        runner.send_keys('c')    # 'c' selects "Close"
        time.sleep(0.5)
        
        # 3. Verify event close_window was dispatched and window 0 selected
        runner.assert_in_log("Dispatching close_window event.", timeout=2.0)
        runner.assert_in_log("Selecting window: 0", timeout=2.0)
        
    except Exception as e:
        print("TEST FAILED! Log contents:")
        print(runner.get_log())
        raise e
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_file_close_menu()
    print("test_file_close_menu passed!")
