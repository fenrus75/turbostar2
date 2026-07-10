from turbostar_runner import *
import time

def test_replace_functionality():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Setup text with banana
        content = "banana apple banana cherry\nbanana grape"
        runner.send_keys(content)
        
        # Move to top
        runner.send_ctrlk('u')
        runner.assert_cursor_position(1, 1)
        
        # 2. Test status bar search with replace option 'r'
        # Start search for "banana"
        runner.send_ctrlk('f')
        runner.send_keys("banana\n")
        # Now options prompt: type 'r' and hit Enter
        runner.send_keys("r\n")
        time.sleep(0.5)
        
        # The Replace dialog should be open, pre-filled with query "banana"
        # We want to tab to the replacement field and enter "orange"
        runner.send_keys('\t')
        time.sleep(0.1)
        runner.send_keys(KEY_CTRL_Y) # Clear pre-filled
        runner.send_keys("orange")
        
        # Now let's trigger Change All (hotkey 'a' -> Alt-A)
        runner.send_keys('\x1b' + 'a')
        time.sleep(0.5)
        
        # Move to top to verify replacements
        runner.send_ctrlk('u')
        
        # First, search for "orange" to set it as current
        runner.send_ctrlk('f')
        runner.send_keys("orange\n\n")
        runner.assert_cursor_position(1, 1, timeout=1.5)
        
        # Search next orange
        runner.send_keys(KEY_CTRL_L)
        runner.assert_cursor_position(1, 14, timeout=1.5)
        
        # Search next orange
        runner.send_keys(KEY_CTRL_L)
        runner.assert_cursor_position(2, 1, timeout=1.5)

    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_replace_functionality()
    print("test_replace passed!")
