from turbostar_runner import TurbostarRunner
import time
import os

def test_dialog_save_load():
    runner = TurbostarRunner()
    project_root = os.environ.get('PROJECT_ROOT', os.getcwd())
    test_file = os.path.join(project_root, 'testrun', "test_dialog_output.txt")
    if os.path.exists(test_file):
        os.remove(test_file)
        
    try:
        runner.start()
        # 1. Type some unique text
        unique_text = "Dialog System Verification"
        runner.send_keys(unique_text)
        runner.assert_text_on_screen(unique_text)
        
        # 2. Open Save As dialog (^KW)
        runner.send_ctrlk('w')
        runner.assert_text_on_screen('Save File As', timeout=2.0)
        
        # 3. Type filename and press Enter
        # Clear pre-filled "unknown.txt"
        runner.send_keys('\x7f', count=25) 
        runner.send_keys(test_file + '\n')
        runner.assert_text_not_on_screen('Save File As', timeout=2.0)
        
        # 4. Verify file exists and has correct content
        runner.assert_file_contains(test_file, unique_text)
            
        # 5. Clear document
        runner.send_keys('\x19', count=5) 
            
        # 6. Open Load dialog (^KE)
        runner.send_ctrlk('e')
        runner.assert_text_on_screen('Open File', timeout=2.0)
        runner.send_keys('\x7f', count=25) # Clear again
        runner.send_keys(test_file + '\n')
        runner.assert_text_not_on_screen('Open File', timeout=2.0)
        
        # 7. Verify text is back
        runner.assert_text_on_screen(unique_text)
        
        runner.send_ctrlk('q')
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()
        if os.path.exists(test_file):
            os.remove(test_file)

if __name__ == "__main__":
    test_dialog_save_load()
    print("test_dialogs passed!")
