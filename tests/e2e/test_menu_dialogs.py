from turbostar_runner import *
import time
import os

def test_menu_save_load():
    runner = TurbostarRunner()
    project_root = os.environ.get('PROJECT_ROOT', os.getcwd())
    test_file = os.path.join(project_root, 'testrun', "test_menu_output.txt")
    if os.path.exists(test_file):
        os.remove(test_file)
        
    try:
        runner.start()
        # 1. Type some unique text
        unique_text = "Menu System Verification"
        runner.send_keys(unique_text)
        runner.assert_text_on_screen(unique_text)
        
        # 2. Open File -> Save via menu
        runner.send_keys('\x1bf') # Alt-F
        runner.send_keys('s')    # 's' for Save
        runner.assert_text_on_screen('Save File As', timeout=2.0)
        
        # 3. Type filename in dialog and press Enter
        runner.send_keys(KEY_BACKSPACE, count=15) # Clear "unknown.txt"
        runner.send_keys(test_file + '\n')
        runner.assert_text_not_on_screen('Save File As', timeout=2.0)
        
        # 4. Verify file exists
        runner.assert_file_contains(test_file, unique_text)
            
        # 5. Clear document using Ctrl-Y
        runner.send_keys(KEY_CTRL_Y, count=5) 
            
        # 6. Open File -> Open via menu
        runner.send_keys('\x1bf') # Alt-F
        runner.send_keys('o')    # 'o' for Open
        runner.assert_text_on_screen('Open File', timeout=2.0)
        
        runner.send_keys(KEY_BACKSPACE, count=25) # Clear
        runner.send_keys(test_file + '\n')
        runner.assert_text_not_on_screen('Save File As', timeout=2.0)
        
        # 7. Verify text is restored
        runner.assert_text_on_screen(unique_text)
        
        
    finally:
        runner.cleanup()
        if os.path.exists(test_file):
            os.remove(test_file)

if __name__ == "__main__":
    test_menu_save_load()
    print("test_menu_dialogs passed!")
