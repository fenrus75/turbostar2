import os
import tempfile
import time
from turbostar_runner import TurbostarRunner, KEY_ESC

def test_debugger_runs_in_foreground():
    # 1. Create a temp home directory
    temp_home = tempfile.mkdtemp(prefix="turbostar_test_debugger_home_")
    
    # 2. Write configuration to .turbostar
    config_path = os.path.join(temp_home, '.turbostar')
    with open(config_path, 'w') as f:
        f.write("main_executable=/bin/bash\n")
        f.write("run_target_mode=window\n")
        f.write("gdb_auto_continue=true\n")
        f.write("run_arguments=-c \"echo hello_world_test_debugger && sleep 2\"\n")

    runner = TurbostarRunner()
    try:
        # Start editor with isolation and the specified HOME
        runner.start(home_dir=temp_home)
        
        # We start with an empty document window.
        # Send key Alt+R to open Run menu
        runner.send_keys(KEY_ESC + 'r')
        runner.assert_text_on_screen("Run in Debugger", timeout=2.0)
        runner.send_keys('d') # Select "Run in Debugger"
        
        # Wait and verify that the run output text actually appears on the screen.
        runner.assert_text_on_screen("hello_world_test_debugger", timeout=5.0)
        
        # Also assert that the debugger split titles are shown
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
        import shutil
        shutil.rmtree(temp_home, ignore_errors=True)

if __name__ == "__main__":
    test_debugger_runs_in_foreground()
    print("test_debugger passed!")
