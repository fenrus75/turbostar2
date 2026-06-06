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
        
        # Verify the prompt text is visible on the screen in history
        runner.assert_text_on_screen("Hello agent!", timeout=2.0)
        
        # Wait for the agent's response or error to start and shift the layout
        start_time = time.time()
        found = False
        while time.time() - start_time < 15.0:
            runner._read_output()
            if any("Hello!" in line or "Error:" in line for line in runner.screen.display):
                found = True
                break
            time.sleep(0.1)
        if not found:
            display_str = "\n".join(runner.screen.display)
            raise AssertionError(f"Neither 'Hello!' nor 'Error:' found on screen after 15.0s. Screen content:\n{display_str}")
        
        # Find which line on the screen contains the prompt
        target_row = -1
        for idx, line in enumerate(runner.screen.display):
            if "Hello agent!" in line:
                target_row = idx
                break
                
        if target_row == -1:
            raise AssertionError("Could not find prompt line on screen")
            
        print(f"Found prompt line at 0-based screen row {target_row}")
        
        # SGR coordinate is 1-based: x_sgr = x_display + 1, y_sgr = y_display + 1
        # Click at the first character of "Hello agent!".
        # In the layout:
        # border: 1 char
        # prefix: 2 chars
        # user message prefix: 2 chars ("> ")
        # So "Hello agent!" starts at 0-based x = 5, which is x_sgr = 6.
        # The y coordinate is target_row, which is y_sgr = target_row + 1.
        y_sgr = target_row + 1
        
        # Click at 'H': x_sgr = 6
        runner.send_raw_keys(f"\x1b[<0;6;{y_sgr}M".encode())
        time.sleep(0.05)
        
        # Drag to after '!': length is 12, so end x_sgr = 6 + 12 = 18
        runner.send_raw_keys(f"\x1b[<32;18;{y_sgr}M".encode())
        time.sleep(0.05)
        
        # Release mouse: x_sgr = 18
        runner.send_raw_keys(f"\x1b[<0;18;{y_sgr}m".encode())
        time.sleep(0.1)
        
        # 3. Quit editor cleanly, discarding changes
        runner.send_keys(KEY_ESC + '1') # Focus main editor document
        time.sleep(0.2)
        runner.send_ctrlk('q')
        runner.wait(timeout=5)
        
        # 4. Verify that the clipboard OSC 52 sequence was output
        # Base64 for "Hello agent!" is "SGVsbG8gYWdlbnQh".
        expected_seq = b"\x1b]52;c;SGVsbG8gYWdlbnQh\x07"
        if expected_seq not in runner.captured_bytes:
            print(f"Captured bytes count: {len(runner.captured_bytes)}")
            print(f"Last 200 captured bytes: {runner.captured_bytes[-200:]}")
            if os.path.exists(runner.log_path):
                with open(runner.log_path, 'r') as f:
                    print("--- EDITOR LOG ---")
                    print(f.read())
            raise AssertionError(f"Expected clipboard sequence {expected_seq} not found in output.")
            
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_agent_mouse_copy()
    print("test_agent_mouse_copy passed!")
