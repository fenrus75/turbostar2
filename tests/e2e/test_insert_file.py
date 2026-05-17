import time
import os
from turbostar_runner import TurbostarRunner

def test_insert_file():
    runner = TurbostarRunner()
    project_root = os.environ.get('PROJECT_ROOT', os.getcwd())
    testrun_dir = os.path.join(project_root, 'testrun')
    os.makedirs(testrun_dir, exist_ok=True)
    
    # 1. Create a file to be inserted
    insert_filename = "to_insert.txt"
    insert_path = os.path.join(testrun_dir, insert_filename)
    with open(insert_path, "w") as f:
        f.write("INSERTED_TEXT")
        
    try:
        runner.start()
        # 2. Type some initial text
        runner.send_keys("Start-")
        runner.assert_text_on_screen("Start-")
        
        # 3. Trigger Insert File (^KR)
        runner.send_ctrlk('r')
        time.sleep(0.5) # Wait for dialog
        
        # 4. Type filename and confirm
        # Clear default and type path
        runner.send_keys('\x7f', count=20)
        runner.send_keys(insert_filename + '\n')

        # 5. Verify text is inserted
        runner.assert_text_on_screen("Start-INSERTED_TEXT", timeout=1.5)
        
        # 6. Test Undo of insert file
        runner.send_keys('\x1f') # Ctrl-_ (Undo)

        runner.assert_text_on_screen("Start-", timeout=1.5)
        # Ensure INSERTED_TEXT is GONE
        runner.assert_text_not_on_screen("INSERTED_TEXT")
        
        
    finally:
        runner.cleanup()
        if os.path.exists(insert_path):
            os.remove(insert_path)

if __name__ == "__main__":
    test_insert_file()
    print("test_insert_file passed!")
