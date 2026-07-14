import time
import os
import re
import base64
from turbostar_runner import *

def test_double_click_copy():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # 1. Insert some text into the empty document
        text_lines = [
            "This is a test line.",
            "Another line with word to copy.",
            "",
            "Paragraph start.",
            "Paragraph middle line.",
            "Paragraph end.",
            ""
        ]
        
        for line in text_lines:
            for char in line:
                runner.send_keys(char)
            runner.send_keys('\n')
            
        time.sleep(0.5)
        
        # 2. Double-click inside the word "word" on line 2 (row 3 on screen)
        # "Another line with word to copy."
        # Character 'r' in "word" is at index 20.
        # Click at screen column 21 (x = 21, y = 3)
        runner.send_mouse_click(21, 3)
        time.sleep(0.05)
        runner.send_mouse_click(21, 3)
        
        # Wait up to 2 seconds for clipboard sequence prefix to be captured
        start_wait = time.time()
        while time.time() - start_wait < 2.0:
            if b"\x1b]52;c;" in runner.captured_bytes:
                break
            time.sleep(0.05)
            runner._read_output()
            
        found_word = False
        matches = re.findall(b"\x1b\\]52;c;([a-zA-Z0-9+/=]+)\x07", runner.captured_bytes)
        if matches:
            copied_base64 = matches[-1].decode('utf-8')
            try:
                copied_text = base64.b64decode(copied_base64).decode('utf-8').strip()
                print(f"Double-click clipboard text: '{copied_text}'")
                if copied_text == "word":
                    found_word = True
            except Exception as e:
                print(f"Decode error: {e}")
                
        assert found_word, "Expected word 'word' on clipboard after double click"
        
        # Clear captured bytes for next test
        runner.captured_bytes = bytearray()
        
        # 3. Triple-click on the paragraph starting at line 4 (row 5 on screen)
        # Click at screen column 5 (x = 5, y = 5)
        runner.send_mouse_click(5, 5)
        time.sleep(0.05)
        runner.send_mouse_click(5, 5)
        time.sleep(0.05)
        runner.send_mouse_click(5, 5)
        
        # Wait up to 2 seconds for clipboard sequence prefix to be captured
        start_wait = time.time()
        while time.time() - start_wait < 2.0:
            # We want at least one clipboard match to appear
            if b"\x1b]52;c;" in runner.captured_bytes:
                break
            time.sleep(0.05)
            runner._read_output()
            
        # Wait a tiny bit extra to let subsequent releases finish writing
        time.sleep(0.1)
        runner._read_output()
            
        found_paragraph = False
        matches = re.findall(b"\x1b\\]52;c;([a-zA-Z0-9+/=]+)\x07", runner.captured_bytes)
        if matches:
            copied_base64 = matches[-1].decode('utf-8')
            try:
                copied_text = base64.b64decode(copied_base64).decode('utf-8').strip()
                print(f"Triple-click clipboard text: '{copied_text}'")
                if "Paragraph start." in copied_text and "Paragraph middle line." in copied_text and "Paragraph end." in copied_text:
                    found_paragraph = True
            except Exception as e:
                print(f"Decode error: {e}")
                
        assert found_paragraph, "Expected consecutive paragraph lines on clipboard after triple click"
        
        # 4. Clean exit
        runner.send_ctrlk('q')
        time.sleep(0.2)
        runner.send_keys('\x1b' + 'd') # Discard changes
        runner.wait(timeout=5)
        
    except Exception as e:
        if hasattr(runner, 'log_path') and os.path.exists(runner.log_path):
            with open(runner.log_path, 'r') as f:
                print("--- EDITOR LOG ---")
                print(f.read())
        print(f"FAILED. Screen:\n{chr(10).join(runner.screen.display)}")
        raise e
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_double_click_copy()
    print("test_double_click_copy passed!")
