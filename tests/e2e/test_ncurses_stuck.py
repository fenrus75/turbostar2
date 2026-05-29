import os
import tempfile
import time
from turbostar_runner import TurbostarRunner, KEY_ESC

def test_ncurses_app_does_not_hang():
    # 1. Create a temp home directory in the project's build directory to avoid systemd /tmp namespace masking
    project_root = os.environ.get('PROJECT_ROOT', os.getcwd())
    build_dir = os.path.join(project_root, 'build')
    temp_home = tempfile.mkdtemp(prefix="turbostar_test_ncurses_home_", dir=build_dir)
    
    # 2. Write configuration to .turbostar
    config_path = os.path.join(temp_home, '.turbostar')
    with open(config_path, 'w') as f:
        # Run python3 which initializes curses and then exits
        f.write("main_executable=python3\n")
        f.write("run_target_mode=window\n")
        # Python script that initializes curses, prints a line, and exits
        script = (
            "import curses, sys, time; "
            "stdscr = curses.initscr(); "
            "stdscr.clear(); "
            "stdscr.addstr(0, 0, 'NCURSES_APP_RUNNING'); "
            "stdscr.refresh(); "
            "time.sleep(1); "
            "curses.endwin(); "
            "print('NCURSES_APP_FINISHED')"
        )
        f.write(f"run_arguments=-c \"{script}\"\n")

    runner = TurbostarRunner()
    try:
        # Start editor with isolation and the specified HOME
        runner.start(home_dir=temp_home)
        
        # Send key Alt+R to open Run menu
        runner.send_keys(KEY_ESC + 'r')
        runner.assert_text_on_screen("Run Settings", timeout=2.0)
        # Select "Run" (r)
        runner.send_keys('r')
        
        # Verify that Run Output window appears
        runner.assert_text_on_screen("Run Output", timeout=2.0)
        
        # Assert that it does not hang and we see NCURSES_APP_FINISHED or NCURSES_APP_RUNNING
        # We give it a reasonable timeout (e.g. 5 seconds) to see if it finishes/displays.
        runner.assert_text_on_screen("NCURSES_APP_FINISHED", timeout=5.0)

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

def test_ncurses_app_in_debugger_does_not_hang():
    # 1. Create a temp home directory
    project_root = os.environ.get('PROJECT_ROOT', os.getcwd())
    build_dir = os.path.join(project_root, 'build')
    temp_home = tempfile.mkdtemp(prefix="turbostar_test_ncurses_gdb_home_", dir=build_dir)
    
    # 2. Write configuration to .turbostar
    config_path = os.path.join(temp_home, '.turbostar')
    with open(config_path, 'w') as f:
        f.write("main_executable=python3\n")
        f.write("run_target_mode=window\n")
        f.write("gdb_auto_continue=true\n")
        script = (
            "import curses, sys, time; "
            "stdscr = curses.initscr(); "
            "stdscr.clear(); "
            "stdscr.addstr(0, 0, 'NCURSES_APP_RUNNING'); "
            "stdscr.refresh(); "
            "time.sleep(1); "
            "curses.endwin(); "
            "print('NCURSES_APP_FINISHED')"
        )
        f.write(f"run_arguments=-c \"{script}\"\n")

    runner = TurbostarRunner()
    try:
        # Start editor with isolation and the specified HOME
        runner.start(home_dir=temp_home)
        
        # Send key Alt+R to open Run menu
        runner.send_keys(KEY_ESC + 'r')
        runner.assert_text_on_screen("Run Settings", timeout=2.0)
        # Select "Run in Debugger" (d)
        runner.send_keys('d')
        
        # Verify that Run Output and Debugger (GDB) windows appear
        runner.assert_text_on_screen("Run Output", timeout=2.0)
        runner.assert_text_on_screen("Debugger (GDB)", timeout=2.0)
        
        # Assert that it does not hang and we see NCURSES_APP_FINISHED
        runner.assert_text_on_screen("NCURSES_APP_FINISHED", timeout=10.0)

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
    test_ncurses_app_does_not_hang()
    test_ncurses_app_in_debugger_does_not_hang()
    print("All tests passed!")
