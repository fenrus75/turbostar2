import time
import os
from turbostar_runner import *

def test_agent_model_switcher():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # 1. Open Agent Window via Alt+A -> O
        runner.send_keys(KEY_ESC + 'a')
        runner.assert_menu_active(timeout=2.0)
        runner.send_keys('o')
        
        # Wait for agent window to appear
        runner.assert_text_on_screen("Agent Chat", timeout=2.0)
        time.sleep(2.0)
        
        # 2. Type /model command
        # Make sure we are at the start of the line and it's clear
        runner.send_keys(KEY_CTRL_A)
        runner.send_keys(KEY_BACKSPACE * 20)
        
        for char in "/model":
            runner.send_keys(char)
            time.sleep(0.1)
        runner.send_keys('\n')
        
        # 3. Verify Model Selection Dialog is open
        runner.assert_text_on_screen("Select Model", timeout=5.0)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_agent_model_switcher()
    print("test_agent_model_switcher passed!")
