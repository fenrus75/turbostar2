import time
from turbostar_runner import *

def test_agent_window_single():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Open Agent Window via Alt+A -> O
        runner.send_keys(KEY_ESC + 'a')
        runner.assert_menu_active(timeout=2.0)
        runner.send_keys('o')
        
        # Wait for agent window to appear
        runner.assert_text_on_screen("Agent Chat", timeout=2.0)
        time.sleep(0.5)
        
        # 2. Open a new editor window via Alt+F -> N
        runner.send_keys(KEY_ESC + 'f')
        runner.assert_menu_active(timeout=2.0)
        runner.send_keys('n')
        time.sleep(0.5)

        # 3. Open Agent Window again via Alt+A -> O
        # This should activate the existing Agent Chat window, not open a new one
        runner.send_keys(KEY_ESC + 'a')
        runner.assert_menu_active(timeout=2.0)
        runner.send_keys('o')
        time.sleep(0.5)

        # 4. Open Window menu (Alt+W) to verify active windows list
        runner.send_keys(KEY_ESC + 'w')
        runner.assert_menu_active(timeout=2.0)

        # The list of windows in the menu should be:
        # "1 noname.txt", "2 Agent Chat", "3 noname.txt"
        # There should NOT be a fourth window "4 Agent Chat"
        runner.assert_text_on_screen("1 noname.txt", timeout=2.0)
        runner.assert_text_on_screen("2 Agent Chat", timeout=2.0)
        runner.assert_text_on_screen("3 noname.txt", timeout=2.0)
        
        # Verify "4 Agent Chat" or "4 noname.txt" is NOT present (indicating no 4th window)
        runner.assert_text_not_on_screen("4 Agent Chat", timeout=2.0)

    except Exception as e:
        print(f"FAILED. Screen:\n{chr(10).join(runner.screen.display)}")
        raise e
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_agent_window_single()
    print("test_agent_window_single passed!")
