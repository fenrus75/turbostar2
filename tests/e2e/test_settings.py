import time
import os
from turbostar_runner import TurbostarRunner

def test_settings_dialog():
    runner = TurbostarRunner()
    project_root = os.environ.get('PROJECT_ROOT', os.getcwd())
    config_path = os.path.join(os.environ.get('HOME', '.'), '.turbostar')
    
    if os.path.exists(config_path):
        os.remove(config_path)

    try:
        runner.start()
        time.sleep(0.5)
        
        # 1. Open Settings Dialog via Alt+P -> P
        runner.send_keys('\x1b' + 'p') # Alt+P
        time.sleep(0.5)
        runner.send_keys('p')
        time.sleep(0.5)
        
        # Verify dialog is open
        runner.assert_text_on_screen("Preferences")
        runner.assert_text_on_screen("Clang Format Style")
        
        # 2. Select "Google" style (hotkey 'G')
        runner.send_keys('g')
        time.sleep(0.2)
        
        # 3. Confirm with Enter
        runner.send_keys('\n')
        time.sleep(0.5)
        
        # 4. Quit and verify config file
        runner.send_keys('\x0b' + 'q') # ^K Q
        runner.wait(timeout=2)
        
        # Verify persistence
        assert os.path.exists(config_path)
        with open(config_path, 'r') as f:
            content = f.read()
            assert "clang_format_style=Google" in content
            
    finally:
        runner.cleanup()
        if os.path.exists(config_path):
            os.remove(config_path)

if __name__ == "__main__":
    test_settings_dialog()
    print("test_settings passed!")
