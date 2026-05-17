import time
import os
from turbostar_runner import TurbostarRunner

def test_settings_dialog():
    runner = TurbostarRunner()
    try:
        runner.start()
        config_path = os.path.join(runner.temp_home, '.turbostar')
        
        # 1. Open Settings Dialog via Alt+P -> P
        runner.send_keys('\x1b' + 'p') # Alt+P
        runner.assert_menu_active(timeout=2.0)
        runner.send_keys('p')

        # Verify dialog is open
        runner.assert_text_on_screen("Preferences", timeout=1.5)
        runner.assert_text_on_screen("Clang Format Style")
        
        # 2. Select "Google" style (hotkey 'G')
        runner.send_keys('g')
        
        # 3. Confirm with Enter
        runner.send_keys('\n')
        
        # 4. Quit and verify config file
        
        # Verify persistence
        runner.assert_file_exists(config_path, timeout=2.0)
        with open(config_path, 'r') as f:
            content = f.read()
            runner.assert_file_contains(config_path, "clang_format_style=Google", timeout=2.0)
            
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_settings_dialog()
    print("test_settings passed!")