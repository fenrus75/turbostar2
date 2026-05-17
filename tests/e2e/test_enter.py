from turbostar_runner import TurbostarRunner
import time

def test_enter_split():
    runner = TurbostarRunner()
    try:
        runner.start()
        # Type "Hello"
        runner.send_keys("Hello")
        runner.assert_text_on_screen("Hello")
        
        # Press Enter to split
        runner.send_keys('\n')
        
        # Verify cursor moved to next line, start of line
        runner.assert_cursor_position(2, 1)
        
        # Type "World"
        runner.send_keys("World")
        runner.assert_text_on_screen("Hello")
        runner.assert_text_on_screen("World")
        
        # Verify cursor position
        runner.assert_cursor_position(2, 6)
        
        # Go back to line 1, end of "Hello"
        runner.send_keys('\x1b[A') # Up
        runner.assert_cursor_position(1, 6)
        
        # Insert "Beautiful " in the middle of split
        # Move to 1:1
        for _ in range(5):
            runner.send_keys('\x1b[D') # Left
        runner.assert_cursor_position(1, 1)
        
        runner.send_keys("Beautiful ")
        runner.assert_text_on_screen("Beautiful Hello")
        runner.assert_cursor_position(1, 11)
        
        runner.send_keys('\n') # Split again
        runner.assert_cursor_position(2, 1)
        runner.assert_text_on_screen("Hello")
        runner.assert_text_on_screen("World")
        
        runner.send_ctrlk('q') # Ctrl-C
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_enter_split()
    print("test_enter_split passed!")
