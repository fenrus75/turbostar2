import time
import os
import json
from turbostar_runner import *

def test_model_config():
    runner = TurbostarRunner()
    try:
        runner.start()
        models_path = os.path.join(runner.temp_home, '.cache', 'turbostar', 'models.json')
        
        # 1. Open Models Dialog via Alt+P -> M
        runner.send_keys(KEY_ESC + 'p') # Alt+P
        runner.assert_menu_active(timeout=2.0)
        runner.send_keys('m')

        # Verify list dialog is open
        runner.assert_text_on_screen("AI Models", timeout=1.5)
        runner.assert_text_on_screen("local-default")
        
        # 2. Add a new model (hotkey 'A')
        runner.send_keys(KEY_ESC + 'a')
        runner.assert_text_on_screen("Add Model", timeout=1.5)
        
        # Fill in fields
        runner.send_keys("test-model")
        runner.send_keys('\t')
        runner.send_keys("Test AI")
        runner.send_keys('\t')
        runner.send_keys("http://localhost:1234")
        runner.send_keys('\t')
        runner.send_keys("secret-key")
        
        # Confirm with OK (hotkey 'O')
        runner.send_keys(KEY_ESC + 'o')
        
        # Verify we are back in the list and the new model is there
        runner.assert_text_on_screen("AI Models", timeout=1.5)
        runner.assert_text_on_screen("test-model")
        
        # 3. Edit the new model
        # The list has 5 items now: claude, gemini, gpt, local, test-model (alphabetical)
        # We start at index 0. We need 4 downs to get to index 4 (test-model).
        for _ in range(4):
            runner.send_keys(KEY_DOWN)
            time.sleep(0.1)
            
        runner.send_keys(KEY_ESC + 'e') # Edit
        runner.assert_text_on_screen("Edit Model", timeout=1.5)
        runner.assert_text_on_screen("test-model")
        
        # Change Name
        runner.send_keys('\t') # Move to Name
        runner.send_keys(KEY_BACKSPACE * 10)
        runner.send_keys("Updated Name")
        
        # Confirm with OK
        runner.send_keys(KEY_ESC + 'o')
        runner.assert_text_on_screen("Updated Name")
        
        # 4. Set as Default (hotkey 'S')
        # We need to re-select it because the dialog was recreated
        for _ in range(4):
            runner.send_keys(KEY_DOWN)
            time.sleep(0.1)
            
        runner.send_keys(KEY_ESC + 's')
        # Should now have '*' prefix
        runner.assert_text_on_screen("* test-model")
        
        # 5. Delete model (hotkey 'D')
        runner.send_keys(KEY_ESC + 'd')
        # Should be gone
        # (Using a small delay to ensure update)
        time.sleep(0.5)
        # runner.assert_text_not_on_screen("test-model") # Need to implement assert_text_not_on_screen?
        
        # 6. Verify persistence in filesystem
        # Close dialog
        runner.send_keys(KEY_ESC + 'c')
        
        runner.assert_file_exists(models_path, timeout=2.0)
        with open(models_path, 'r') as f:
            data = json.load(f)
            # Find gpt-4o (baseline)
            found = False
            for item in data:
                if item['id'] == 'gpt-4o':
                    found = True
                    break
            assert found, "gpt-4o should be in models.json"
            
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_model_config()
    print("test_model_config passed!")
