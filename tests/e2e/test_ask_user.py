from turbostar_runner import *
import time
import json

def test_ask_user():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # Open Agent window
        runner.send_keys(f"{KEY_ESC}a")
        runner.assert_text_on_screen("Agent Chat")
        
        # We simulate the LLM backend triggering the ask_user tool by injecting a payload
        # Wait, the easiest way to trigger it is to type a specific command if we had a test agent,
        # but since we are E2E testing the UI, we can just send the `prompt_user` event by
        # somehow invoking the tool.
        # Actually, our turbostar_runner can't easily inject internal C++ events.
        # The E2E tests usually test user actions.
        # Let's see if there's an agent command or if we can use a test plugin.
        pass
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_ask_user()
