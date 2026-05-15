from turbostar_runner import TurbostarRunner
import time
import os

def test_menu_save_load():
    runner = TurbostarRunner()
    test_file = "test_menu_output.txt"
    if os.path.exists(test_file):
        os.remove(test_file)
        
    try:
        runner.start()
        time.sleep(0.5)
        
        # 1. Type some unique text
        unique_text = "Menu System Verification"
        runner.send_keys(unique_text)
        runner.assert_text_on_screen(unique_text)
        
        # 2. Open File -> Save via menu
        runner.send_keys('\x1bf') # Alt-F
        runner.send_keys('s')    # 's' for Save
        
        # 3. Type filename in dialog and press Enter
        runner.send_keys('\x7f' * 15) # Clear "unknown.txt"
        runner.send_keys(test_file + '\n')
        time.sleep(0.5)
        
        # 4. Verify file exists
        assert os.path.exists(test_file)
        with open(test_file, 'r') as f:
            content = f.read()
            assert unique_text in content
            
        # 5. Clear document using Ctrl-Y
        for _ in range(5):
            runner.send_raw_keys('\x19') 
            
        # 6. Open File -> Open via menu
        runner.send_keys('\x1bf') # Alt-F
        runner.send_keys('o')    # 'o' for Open
        
        runner.send_keys('\x7f' * 25) # Clear
        runner.send_keys(test_file + '\n')
        time.sleep(0.5)
        
        # 7. Verify text is restored
        runner.assert_text_on_screen(unique_text)
        
        runner.send_keys('\x0b' + 'q')
        runner.wait(timeout=2)
        
    finally:
        runner.cleanup()
        if os.path.exists(test_file):
            os.remove(test_file)

if __name__ == "__main__":
    test_menu_save_load()
    print("test_menu_dialogs passed!")
