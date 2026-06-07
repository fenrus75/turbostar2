import time
from turbostar_runner import *

def test_menus_open_close():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # Test File menu
        runner.send_keys(KEY_ESC + 'f')
        runner.assert_text_on_screen("New", timeout=2.0)
        runner.send_keys(KEY_ESC)
        time.sleep(0.1)
        runner.assert_in_log("Menu activated: File")
        
        # Test Edit menu
        runner.send_keys(KEY_ESC + 'e')
        runner.assert_text_on_screen("Undo", timeout=2.0)
        runner.send_keys(KEY_ESC)
        time.sleep(0.1)
        runner.assert_in_log("Menu activated: Edit")
        
        # Test Search menu
        runner.send_keys(KEY_ESC + 's')
        runner.assert_text_on_screen("Find...", timeout=2.0)
        runner.send_keys(KEY_ESC)
        time.sleep(0.1)
        runner.assert_in_log("Menu activated: Search")
        
        # Test Help menu
        runner.send_keys(KEY_ESC + 'h')
        runner.assert_text_on_screen("Help Index", timeout=2.0)
        runner.send_keys(KEY_ESC)
        time.sleep(0.1)
        runner.assert_in_log("Menu activated: Help")
    except Exception as e:
        print(f"FAILED. Log: {runner.get_log()}")
        print(f"Screen:\n{chr(10).join(runner.screen.display)}")
        raise e
    finally:
        runner.cleanup()

def test_file_exit():
    runner = TurbostarRunner()
    try:
        runner.start()
        runner.send_keys('\x1bf') # Alt-F
        runner.assert_text_on_screen('Exit')
        runner.send_keys('x')    # 'x' selects the "Exit" item

        runner.assert_in_log("Menu activated: File")
        runner.assert_in_log("Dispatching quit event.")
    finally:
        runner.cleanup()

def test_run_menu_shading():
    import tempfile
    import shutil
    import os

    # 1. Start with NO main executable configured (empty home)
    temp_home = tempfile.mkdtemp(prefix="turbostar_test_menu_shading_")
    runner = TurbostarRunner()
    try:
        runner.start(home_dir=temp_home)
        runner.assert_text_on_screen("=1=", timeout=2.0)

        # Open Run menu (Alt+R / Esc+r)
        runner.send_keys(KEY_ESC + 'r')
        runner.assert_text_on_screen("Run in Debugger", timeout=2.0)

        # Since it's disabled, pressing 'd' should do nothing, menu stays open
        runner.send_keys('d')
        time.sleep(0.1)
        # Verify menu is still open by asserting "Run Settings..." is visible
        runner.assert_text_on_screen("Run Settings...", timeout=1.0)

        # Press ESC to close menu
        runner.send_keys(KEY_ESC)
        time.sleep(0.1)
        # Verify menu closed
        runner.assert_text_not_on_screen("Run Settings...", timeout=1.0)
    finally:
        runner.cleanup()
        shutil.rmtree(temp_home, ignore_errors=True)

    # 2. Start WITH main executable configured
    temp_home = tempfile.mkdtemp(prefix="turbostar_test_menu_shading_")
    config_path = os.path.join(temp_home, '.turbostar')
    with open(config_path, 'w') as f:
        f.write("main_executable=/bin/bash\n")
        f.write("run_target_mode=window\n")
        f.write("gdb_auto_continue=true\n")
        f.write("run_arguments=-c \"echo hello_world && sleep 10\"\n")

    runner = TurbostarRunner()
    try:
        runner.start(home_dir=temp_home)
        runner.assert_text_on_screen("=1=", timeout=2.0)

        # Open Run menu
        runner.send_keys(KEY_ESC + 'r')
        runner.assert_text_on_screen("Run in Debugger", timeout=2.0)

        # Since it is now enabled, pressing 'd' should launch GDB / Debugger
        runner.send_keys('d')
        runner.assert_text_on_screen("Debugger (GDB)", timeout=5.0)

        # Exit application
        runner.send_ctrlk('q')
        runner.wait(timeout=5)
    finally:
        runner.cleanup()
        shutil.rmtree(temp_home, ignore_errors=True)

if __name__ == "__main__":
    test_menus_open_close()
    test_file_exit()
    test_run_menu_shading()
