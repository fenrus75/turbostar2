from turbostar_runner import *
import time

def test_ask_user():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # 1. Open Agent window
        runner.send_keys(f"{KEY_ESC}a")
        time.sleep(0.2)
        runner.send_keys('\n')
        runner.assert_text_on_screen("Agent Chat")
        
        # 2. Ask the LLM to trigger the tool
        # Wait a moment for the LLM client to initialize
        time.sleep(1)
        
        # Send a prompt designed to reliably trigger the tool
        prompt = "Call the ask_user tool with the question 'Confirm status?' and options 'Yes' and 'No'."
        runner.send_keys(prompt)
        runner.send_keys('\n')
        
        # 3. Wait for the tool dialog to appear
        runner.assert_text_on_screen("Question", timeout=15.0)
        runner.assert_text_on_screen("Confirm status?", timeout=1.0)
        runner.assert_text_on_screen("(•) Yes", timeout=1.0) # 'Yes' should be focused initially
        runner.assert_text_on_screen("( ) No", timeout=1.0)
        
        # 4. Navigate the options
        # Move down to 'No'
        runner.send_keys(KEY_DOWN)
        time.sleep(0.5)
        runner.assert_text_on_screen("(•) No", timeout=1.0)
        
        # Move down to 'Other'
        runner.send_keys(KEY_DOWN)
        time.sleep(0.5)
        
        # Type a custom response
        runner.send_keys("Custom Answer")
        time.sleep(0.5)
        runner.assert_text_on_screen("Custom Answer", timeout=1.0)
        
        # Press OK to submit
        runner.send_keys('\n')
        
        # 5. Wait for the dialog to close and the LLM to respond
        # The LLM will receive "Custom Answer" and likely acknowledge it.
        runner.assert_text_not_on_screen("Question", timeout=5.0)
        
    except Exception as e:
        print(f"FAILED. Log:\n{runner.get_log()}")
        raise e
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_ask_user()
