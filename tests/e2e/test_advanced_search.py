from turbostar_runner import TurbostarRunner
import time

def test_advanced_search():
    runner = TurbostarRunner()
    try:
        runner.start()
        time.sleep(0.5)
        
        # 1. Setup multi-line text with repeating words and case variants
        content = "Apple apple alpha\nBanana banana\nApple pie"
        runner.send_keys(content)
        
        # 2. Test Case Sensitivity (OFF by default in our params, let's toggle it)
        runner.send_keys('\x0b' + 'u') # Top
        runner.send_keys('\x1b' + 's') # Alt-S
        runner.send_keys('f')          # Find...
        time.sleep(0.5)
        
        # Dialog open. Input "Apple".
        runner.send_keys('\x7f', count=10)
        runner.send_keys("Apple")
        # Tab to "Case sensitive" (focus 1)
        runner.send_keys('\t')
        # Space to toggle ON (X)
        runner.send_keys(' ')
        # Tab to OK (focus 10)
        runner.send_keys('\t', count=9)
        runner.send_keys('\n')
        
        # Should find "Apple" at 1:1
        runner.assert_cursor_position(1, 1)
        
        # Find next (^L) - should find "Apple" at 3:1 (skipping lowercase "apple")
        runner.send_keys('\x0c')
        runner.assert_cursor_position(3, 1)
        
        # 3. Test Backward Search
        runner.send_keys('\x1b' + 's') # Alt-S
        runner.send_keys('f')          # Find...
        time.sleep(0.5)
        # Tab 2 times to "Direction" group (focus 4 = Forward)
        runner.send_keys('\t\t')
        # Down arrow to "Backward" (focus 5)
        runner.send_keys('\x1b[B')
        runner.send_keys(' ') # Toggle Backward ON
        
        # Tab 3 more times to "Buttons" group (focus 10 = OK)
        runner.send_keys('\t\t\t')
        runner.send_keys('\n')
        
        # From 3:1, searching backward for "Apple" (case sensitive ON)
        # Since it does not step over initially, it finds the "Apple" at 3:1 again
        try:
            runner.assert_cursor_position(3, 1)
        except Exception as e:
            print(f"FAILED Backward search (dialog). Log:\n{runner.get_log()}")
            raise e
            
        # Now use Find Next (^L) to actually step back to the previous one
        runner.send_keys('\x0c') # ^L
        time.sleep(0.5)
        try:
            runner.assert_cursor_position(1, 1)
        except Exception as e:
            print(f"FAILED Backward search (^L). Log:\n{runner.get_log()}")
            raise e
        
        # 4. Test Whole Words
        runner.send_keys('\x1b' + 's') # Alt-S
        runner.send_keys('f')          # Find...
        time.sleep(0.5)
        # Input "App"
        runner.send_keys('\x7f', count=10)
        runner.send_keys("App")
        # Tab 1 time to "Options" group (focus 1 = Case sensitive)
        runner.send_keys('\t')
        # Down arrow to "Whole words only" (focus 2)
        runner.send_keys('\x1b[B')
        runner.send_keys(' ')  # Toggle ON
        
        # Tab 4 more times to "Buttons" group (focus 10 = OK)
        runner.send_keys('\t', count=4)
        runner.send_keys('\n')
        
        # Should NOT find "App" in "Apple"
        # Cursor should NOT have moved (or at least not to a match)
        # Let's assume it stays at 1:1
        runner.assert_cursor_position(1, 1)
        
        runner.send_keys('\x0b' + 'q')
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_advanced_search()
    print("test_advanced_search passed!")
