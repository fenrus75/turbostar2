import time
import os
from turbostar_runner import *

def test_turboagent_no_args():
    runner = TurbostarRunner()
    try:
        # Start using the turboagent symlink, with no filenames
        runner.start(exe_path='./turboagent')
        
        # Verify the Agent Chat window is active immediately
        runner.assert_text_on_screen("Agent Chat", timeout=3.0)
        
        # Verify the welcome screen is NOT active (no OK button / About title)
        runner.assert_text_not_on_screen("About", timeout=1.0)
        
        # Let's open the Window menu (Alt+W) to verify active windows list
        runner.send_keys(KEY_ESC + 'w')
        runner.assert_menu_active(timeout=2.0)
        
        # The list of windows in the menu should only contain the Agent Chat window
        runner.assert_text_on_screen("1 Agent Chat", timeout=2.0)
        runner.assert_text_not_on_screen("2 noname.txt", timeout=1.0)
        
    except Exception as e:
        print(f"FAILED test_turboagent_no_args. Screen:\n{chr(10).join(runner.screen.display)}")
        raise e
    finally:
        runner.cleanup()

def test_turboagent_with_filename():
    runner = TurbostarRunner()
    # Create a dummy file to open
    test_file = "ta.txt"
    try:
        # Start using the turboagent symlink, passing a filename
        runner.start(filename=test_file, exe_path='./turboagent')
        
        # Verify the Agent Chat window is still active immediately
        runner.assert_text_on_screen("Agent Chat", timeout=3.0)
        
        # Open the Window menu (Alt+W) to verify active windows list
        runner.send_keys(KEY_ESC + 'w')
        runner.assert_menu_active(timeout=2.0)
        
        # The list of windows in the menu should contain both the file window and the Agent Chat window
        # The file window is opened first, so it is window 1. Agent Chat is window 2.
        runner.assert_text_on_screen(f"1 {test_file}", timeout=2.0)
        runner.assert_text_on_screen("2 Agent Chat", timeout=2.0)
        
    except Exception as e:
        print(f"FAILED test_turboagent_with_filename. Screen:\n{chr(10).join(runner.screen.display)}")
        raise e
    finally:
        runner.cleanup()
        if os.path.exists(os.path.join(os.environ.get('PROJECT_ROOT', os.getcwd()), 'testrun', test_file)):
            os.remove(os.path.join(os.environ.get('PROJECT_ROOT', os.getcwd()), 'testrun', test_file))

if __name__ == "__main__":
    test_turboagent_no_args()
    test_turboagent_with_filename()
    print("All turboagent startup tests passed!")
