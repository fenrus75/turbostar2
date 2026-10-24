from turbostar_runner import *
import time

def test_extra_shortcuts():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Test ^A and ^E
        runner.send_keys("Hello")
        runner.assert_cursor_position(1, 6)
        
        runner.send_keys(KEY_CTRL_A) # ^A
        runner.assert_cursor_position(1, 1)
        
        runner.send_keys(KEY_CTRL_E) # ^E
        runner.assert_cursor_position(1, 6)
        
        # 2. Test ^D (within line on space)
        runner.send_keys(KEY_CTRL_A) # Back to start
        runner.send_keys(" ")        # Insert leading space -> " Hello"
        runner.send_keys(KEY_CTRL_A) # Back to space
        runner.send_keys(KEY_CTRL_D) # ^D on non-word deletes space -> "Hello"
        runner.assert_text_on_screen("Hello")
        runner.assert_cursor_position(1, 1)
        
        # 3. Test ^D (join line)
        runner.send_keys(KEY_CTRL_E) # End of line
        runner.send_keys('\n')    # New line
        runner.send_keys("World")
        runner.assert_cursor_position(2, 6)
        
        runner.send_keys(KEY_UP) # Up to line 1
        runner.send_keys(KEY_CTRL_E) # End of line 1
        runner.assert_cursor_position(1, 6) # "Hello" is 5 chars, so end is 6
        
        runner.send_keys(KEY_CTRL_D) # ^D at EOL should join "World"
        runner.assert_text_on_screen("HelloWorld")
        runner.assert_cursor_position(1, 6) # Cursor stays at join point
        
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_extra_shortcuts()
    print("test_extra_shortcuts passed!")
