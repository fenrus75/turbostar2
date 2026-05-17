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
        
        # 2. Test ^D (within line)
        runner.send_keys(KEY_CTRL_A) # Back to start
        runner.send_keys(KEY_CTRL_D) # ^D deletes 'H'
        runner.assert_text_on_screen("ello")
        runner.assert_cursor_position(1, 1)
        
        # 3. Test ^D (join line)
        runner.send_keys(KEY_CTRL_E) # End of line
        runner.send_keys('\n')    # New line
        runner.send_keys("World")
        runner.assert_cursor_position(2, 6)
        
        runner.send_keys(KEY_UP) # Up to line 1
        runner.send_keys(KEY_CTRL_E) # End of line 1
        runner.assert_cursor_position(1, 5) # "ello" is 4 chars, so end is 5
        
        runner.send_keys(KEY_CTRL_D) # ^D at EOL should join "World"
        runner.assert_text_on_screen("elloWorld")
        runner.assert_cursor_position(1, 5) # Cursor stays at join point
        
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_extra_shortcuts()
    print("test_extra_shortcuts passed!")
