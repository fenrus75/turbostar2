import os
import tempfile
import time
import shutil
from turbostar_runner import TurbostarRunner, KEY_ESC

def test_linked_windows():
    # 1. Create a temp home directory
    temp_home = tempfile.mkdtemp(prefix="turbostar_test_linked_home_")
    
    # 2. Write configuration to .turbostar
    config_path = os.path.join(temp_home, '.turbostar')
    with open(config_path, 'w') as f:
        f.write("main_executable=/bin/bash\n")
        f.write("run_target_mode=window\n")
        f.write("gdb_auto_continue=true\n")
        f.write("run_arguments=-c \"echo hello_world_linked_windows && sleep 10\"\n")

    runner = TurbostarRunner()
    try:
        # Start editor
        runner.start(home_dir=temp_home)
        runner.assert_text_on_screen("=1=", timeout=2.0)
        
        # 3. Launch the program in the debugger via Alt+R -> 'd'
        runner.send_keys(KEY_ESC + 'r')
        runner.assert_text_on_screen("Run in Debugger", timeout=2.0)
        runner.send_keys('d')
        
        # Wait and verify that both run output and debugger are on screen
        runner.assert_text_on_screen("hello_world_linked_windows", timeout=5.0)
        runner.assert_text_on_screen("Debugger (GDB)", timeout=2.0)
        runner.assert_text_on_screen("Run Output", timeout=2.0)

        # 4. Switch focus to the main editor window using Alt+1 (Esc + 1)
        runner.send_keys(KEY_ESC + '1')
        time.sleep(0.5)

        # Assert that Debugger and Run Output windows are hidden behind the full screen editor window
        runner.assert_text_not_on_screen("Debugger (GDB)", timeout=2.0)
        runner.assert_text_not_on_screen("Run Output", timeout=2.0)

        # 5. Switch focus back to Run Output window using Alt+2 (Esc + 2)
        runner.send_keys(KEY_ESC + '2')
        time.sleep(0.5)

        # Assert that BOTH linked windows are visible again
        runner.assert_text_on_screen("Debugger (GDB)", timeout=2.0)
        runner.assert_text_on_screen("Run Output", timeout=2.0)

        # 6. Switch focus to main editor window again using Alt+1
        runner.send_keys(KEY_ESC + '1')
        time.sleep(0.5)
        runner.assert_text_not_on_screen("Debugger (GDB)", timeout=2.0)

        # 7. Switch focus to Debugger window using Alt+3 (Esc + 3)
        runner.send_keys(KEY_ESC + '3')
        time.sleep(0.5)

        # Assert that BOTH linked windows are visible again
        runner.assert_text_on_screen("Debugger (GDB)", timeout=2.0)
        runner.assert_text_on_screen("Run Output", timeout=2.0)

        # Cleanup: quit the application
        runner.send_ctrlk('q')
        runner.wait(timeout=5)
        
    except Exception as e:
        print(f"TEST FAILED! Event Log:\n{runner.get_log()}")
        raise e
    finally:
        runner.cleanup()
        shutil.rmtree(temp_home, ignore_errors=True)

if __name__ == "__main__":
    test_linked_windows()
    print("test_linked_windows passed!")
