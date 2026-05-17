from turbostar_runner import TurbostarRunner
import time

def test_extra_shortcuts():
    runner = TurbostarRunner()
    try:
        runner.start()
        time.sleep(0.5)
        
        # 1. Test ^A and ^E
        runner.send_keys("Hello")
        runner.assert_cursor_position(1, 6)
        
        runner.send_keys('\x01') # ^A
        runner.assert_cursor_position(1, 1)
        
        runner.send_keys('\x05') # ^E
        runner.assert_cursor_position(1, 6)
        
        # 2. Test ^D (within line)
        runner.send_keys('\x01') # Back to start
        runner.send_keys('\x04') # ^D deletes 'H'
        runner.assert_text_on_screen("ello")
        runner.assert_cursor_position(1, 1)
        
        # 3. Test ^D (join line)
        runner.send_keys('\x05') # End of line
        runner.send_keys('\n')    # New line
        runner.send_keys("World")
        runner.assert_cursor_position(2, 6)
        
        runner.send_keys('\x1b[A') # Up to line 1
        runner.send_keys('\x05') # End of line 1
        runner.assert_cursor_position(1, 5) # "ello" is 4 chars, so end is 5
        
        runner.send_keys('\x04') # ^D at EOL should join "World"
        runner.assert_text_on_screen("elloWorld")
        runner.assert_cursor_position(1, 5) # Cursor stays at join point
        
        runner.send_ctrlk('q') # Ctrl-C
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_extra_shortcuts()
    print("test_extra_shortcuts passed!")
