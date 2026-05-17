from turbostar_runner import *
import time

def test_backspace():
    runner = TurbostarRunner()
    try:
        runner.start()
        # Test 1: Backspace within a line
        runner.send_keys("Helloo")
        runner.assert_text_on_screen("Helloo")
        runner.send_keys(KEY_BACKSPACE) # ASCII 127 for Backspace
        runner.assert_text_on_screen("Hello")
        runner.assert_cursor_position(1, 6)
        
        # Test 2: Backspace at start of line (joining lines)
        runner.send_keys('\n') # Create line 2
        runner.assert_cursor_position(2, 1) # Should be start of line 2
        runner.send_keys("World")
        runner.assert_text_on_screen("World")
        
        # Move to start of line 2
        for _ in range(5):
            runner.send_keys(KEY_LEFT) # Left
        runner.assert_cursor_position(2, 1)
        
        # Press backspace to join "World" to "Hello"
        runner.send_keys(KEY_BACKSPACE)
        runner.assert_text_on_screen("HelloWorld")
        runner.assert_cursor_position(1, 6) # Cursor should be after "Hello"
        
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_backspace()
    print("test_backspace passed!")
