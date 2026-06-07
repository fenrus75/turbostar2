from turbostar_runner import *
import time

def test_advanced_search():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Setup multi-line text with repeating words and case variants
        content = "Apple apple alpha\nBanana banana\nApple pie"
        runner.send_keys(content)
        
        # Go to top
        runner.send_ctrlk('u')
        
        # 2. Test Case Sensitivity (OFF by default)
        runner.send_keys('\x1b' + 's') # Alt-S
        runner.send_keys('f')          # Find...
        time.sleep(0.1)
        
        # Dialog open. Input "Apple".
        runner.send_keys(KEY_CTRL_Y)
        runner.send_keys("Apple")
        
        # Toggle Case Sensitive (hotkey 'c' -> Alt-C)
        runner.send_keys('\x1b' + 'c')
        
        
        # Press OK (hotkey 'k' -> Alt-K)
        runner.send_keys('\x1b' + 'k')
        
        # Should find "Apple" at 1:1
        runner.assert_cursor_position(1, 1)
        
        # Find next (^L) - should find "Apple" at 3:1 (skipping lowercase "apple")
        runner.send_keys(KEY_CTRL_L)
        time.sleep(0.1)
        runner.assert_cursor_position(3, 1)
        
        # 3. Test Backward Search
        runner.send_keys('\x1b' + 's') # Alt-S
        runner.send_keys('f')          # Find...
        time.sleep(0.1)
        
        # Toggle Backward (hotkey 'b' -> Alt-B)
        runner.send_keys('\x1b' + 'b')
        
        
        # Press OK (Alt-K)
        runner.send_keys('\x1b' + 'k')
        
        # From 3:1, searching backward for "Apple" (case sensitive ON)
        # Should stay at 3:1 initially
        runner.assert_cursor_position(3, 1)
            
        # Find next (^L) to step back to previous match
        runner.send_keys(KEY_CTRL_L) # ^L
        time.sleep(0.1)
        runner.assert_cursor_position(1, 1)
        
        # 4. Test Whole Words
        runner.send_keys('\x1b' + 's') # Alt-S
        runner.send_keys('f')          # Find...
        time.sleep(0.1)
        
        # Input "App"
        runner.send_keys(KEY_CTRL_Y)
        runner.send_keys("App")
        
        # Toggle Whole Words (hotkey 'w' -> Alt-W)
        runner.send_keys('\x1b' + 'w')
        
        
        # Press OK (Alt-K)
        runner.send_keys('\x1b' + 'k')
        
        # Should NOT find "App" as a whole word, stays at 1:1
        runner.assert_cursor_position(1, 1)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_advanced_search()
    print("test_advanced_search passed!")
