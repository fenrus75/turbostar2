import time
import os
from turbostar_runner import *

def test_agent_mouse_copy():
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
        
        # 2. Type "Hello agent!" in the agent input box and press Enter
        prompt = "Hello agent!"
        for char in prompt:
            runner.send_keys(char)
        runner.send_keys('\n')
        time.sleep(0.5)
        
        # Prompt sent, wait for the agent's response and ensure the agent is no longer thinking
        # before we attempt to scroll. Otherwise, ongoing thinking updates will keep resetting
        # the scroll offset back to 0.
        start_time = time.time()
        found = False
        while time.time() - start_time < 30.0:
            runner._read_output()
            display_str = "\n".join(runner.screen.display)
            if "Error:" in display_str:
                import sys
                print("LLM returned an error (likely no local LLM running). Skipping test.")
                sys.exit(77)
            
            # Check if Hello! is on screen and Agent is not thinking anymore
            has_hello = any("Hello!" in line for line in runner.screen.display)
            is_thinking = any("Thinking..." in line for line in runner.screen.display)
            if has_hello and not is_thinking:
                found = True
                break
            time.sleep(0.1)
        if not found:
            import sys
            print("LLM did not respond in 30.0s (slow or no local LLM). Skipping test.")
            sys.exit(77)
        
        # Find which line on the screen contains the assistant response
        target_row = -1
        start_col = -1
        for idx, line in enumerate(runner.screen.display):
            if "Hello!" in line:
                target_row = idx
                start_col = line.find("Hello!")
                break
            
        if target_row == -1:
            raise AssertionError("Could not find Hello! on screen")
            
        print(f"Found Hello! line at 0-based screen row {target_row}, col {start_col}")
        
        # SGR coordinate is 1-based: x_sgr = x_display + 1, y_sgr = y_display + 1
        y_sgr = target_row + 1
        x_start_sgr = start_col + 1
        
        # Click at 'H'
        runner.send_raw_keys(f"\x1b[<0;{x_start_sgr};{y_sgr}M".encode())
        time.sleep(0.05)
        
        # Drag to after '!': length is 6, so end x_sgr = x_start_sgr + 6
        x_end_sgr = x_start_sgr + 6
        runner.send_raw_keys(f"\x1b[<32;{x_end_sgr};{y_sgr}M".encode())
        time.sleep(0.05)
        
        # Release mouse
        runner.send_raw_keys(f"\x1b[<0;{x_end_sgr};{y_sgr}m".encode())
        time.sleep(0.1)
        
        # 3. Quit editor cleanly, discarding changes
        runner.send_keys(KEY_ESC + '1') # Focus main editor document
        time.sleep(0.2)
        runner.send_ctrlk('q')
        runner.wait(timeout=5)
        
        # 4. Verify that the clipboard OSC 52 sequence was output
        # Base64 for "Hello!" is "SGVsbG8h".
        expected_seq = b"\x1b]52;c;SGVsbG8h\x07"
        if expected_seq not in runner.captured_bytes:
            print(f"Captured bytes count: {len(runner.captured_bytes)}")
            print(f"Last 200 captured bytes: {runner.captured_bytes[-200:]}")
            if os.path.exists(runner.log_path):
                with open(runner.log_path, 'r') as f:
                    print("--- EDITOR LOG ---")
                    print(f.read())
            raise AssertionError(f"Expected clipboard sequence {expected_seq} not found in output.")
            
    except Exception as e:
        if hasattr(runner, 'log_path') and os.path.exists(runner.log_path):
            import shutil
            shutil.copy(runner.log_path, "/tmp/turbostar_e2e_error.log")
            print("Wrote editor log to /tmp/turbostar_e2e_error.log")
        print(f"FAILED. Screen:\n{chr(10).join(runner.screen.display)}")
        raise e
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_agent_mouse_copy()
    print("test_agent_mouse_copy passed!")
