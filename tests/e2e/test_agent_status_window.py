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
        
        # Verify Agent Status text is on the screen
        runner.assert_text_on_screen("Agent St")
        runner.assert_text_on_screen("Model:")
        runner.assert_text_on_screen("Cost:")
        
    except Exception as e:
        print(f"FAILED. Log: {runner.get_log()}")
        print(f"Screen:\n{chr(10).join(runner.screen.display)}")
        raise e
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_agent_status_window_visibility()