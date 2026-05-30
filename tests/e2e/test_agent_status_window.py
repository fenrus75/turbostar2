import time
from turbostar_runner import *

def test_agent_status_window_visibility():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # Open Agent Menu -> Open Chat...
        runner.send_keys('\x1ba') # Alt-A
        time.sleep(0.5)
        runner.send_keys('o')     # Open Chat
        
        # Wait a bit for the windows to be created and layout to be updated
        time.sleep(0.5)
        
        # Verify Agent Window and Agent Status text are on the screen
        runner.assert_text_on_screen("Agent St")
        runner.assert_text_on_screen("Model:")
        
        # Switch focus back to document =1= (Alt-1 / Esc-1)
        runner.send_keys(KEY_ESC + '1')
        time.sleep(0.5)
        
        # Verify both agent and status windows are hidden (not visible on screen)
        runner.assert_text_not_on_screen("Agent St", timeout=2.0)
        runner.assert_text_not_on_screen("Model:", timeout=2.0)
        
        # Switch focus back to agent =2= (Alt-2 / Esc-2)
        runner.send_keys(KEY_ESC + '2')
        time.sleep(0.5)
        
        # Verify both are visible again
        runner.assert_text_on_screen("Agent St")
        runner.assert_text_on_screen("Model:")
        
        # Switch focus back to document =1=
        runner.send_keys(KEY_ESC + '1')
        time.sleep(0.5)
        runner.assert_text_not_on_screen("Agent St", timeout=2.0)
        
        # Switch focus to agent status =3= (Alt-3 / Esc-3)
        runner.send_keys(KEY_ESC + '3')
        time.sleep(0.5)
        
        # Verify both are visible again
        runner.assert_text_on_screen("Agent St")
        runner.assert_text_on_screen("Model:")
        
    except Exception as e:
        print(f"FAILED. Log: {runner.get_log()}")
        print(f"Screen:\n{chr(10).join(runner.screen.display)}")
        raise e
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_agent_status_window_visibility()