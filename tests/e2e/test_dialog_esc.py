import time
from turbostar_runner import *

def test_dialog_esc():
    runner = TurbostarRunner()
    
    try:
        runner.start()
        
        # 1. Open a dialog (e.g. Find)
        runner.send_keys(KEY_ESC + 's') # Alt-S
        runner.send_keys('f')          # Find...
        time.sleep(0.5)
        
        runner.assert_text_on_screen("Find")
        runner.assert_text_on_screen("Cancel")
        
        # 2. Press ESC
        runner.send_keys(KEY_ESC)
        time.sleep(0.5)
        
        # 3. Assert dialog is closed
        runner.assert_text_not_on_screen("Find", timeout=2.0)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_dialog_esc()
    print("test_dialog_esc passed!")