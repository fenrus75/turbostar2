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
        time.sleep(0.5)
        
        # 1. Type some unique text
        unique_text = "Dialog System Verification"
        runner.send_keys(unique_text)
        runner.assert_text_on_screen(unique_text)
        
        # 2. Open Save As dialog (^KW)
        runner.send_ctrlk('w')
        time.sleep(0.5)
        
        # 3. Type filename and press Enter
        # Clear pre-filled "unknown.txt"
        runner.send_keys('\x7f', count=25) 
        runner.send_keys(test_file + '\n')
        time.sleep(1.0)
        
        # 4. Verify file exists and has correct content
        if not os.path.exists(test_file):
            print(f"ERROR: File {test_file} not found.")
            print(f"Log:\n{runner.get_log()}")
            assert os.path.exists(test_file)
            
        with open(test_file, 'r') as f:
            content = f.read()
            if unique_text not in content:
                print(f"ERROR: Unique text not in file. Content: '{content}'")
                print(f"Log:\n{runner.get_log()}")
                assert unique_text in content
            
        # 5. Clear document
        runner.send_keys('\x19', count=5) 
            
        # 6. Open Load dialog (^KE)
        runner.send_ctrlk('e')
        runner.send_keys('\x7f', count=25) # Clear again
        runner.send_keys(test_file + '\n')
        time.sleep(0.5)
        
        # 7. Verify text is back
        try:
            runner.assert_text_on_screen(unique_text)
        except Exception as e:
            print(f"ERROR: Text not found after load.")
            print(f"Log:\n{runner.get_log()}")
            raise e
        
        runner.send_ctrlk('q')
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()
        if os.path.exists(test_file):
            os.remove(test_file)

if __name__ == "__main__":
    test_dialog_save_load()
    print("test_dialogs passed!")
