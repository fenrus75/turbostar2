from turbostar_runner import TurbostarRunner
import time

def test_tabs_positioning():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Type text with a tab
        runner.send_keys("Tab")
        runner.send_keys('\t')
        runner.send_keys("Text")
        
        time.sleep(0.5)
        
        # Total chars: 3 (Tab) + 1 (\t) + 4 (Text) = 8 chars.
        # Cursor should be at screen coordinate 1:13.
        try:
            runner.assert_cursor_position(1, 13)
        except Exception as e:
            print(f"FAILED at initial typing. Log:\n{runner.get_log()}")
            raise e
        
        # 3. Move back to before 'Text'
        # Currently at index 8 (col 13).
        # T e x t (4 chars)
        runner.send_keys('\x1b[D', count=4) # Left
        # Should be at index 4 (after \t)
        # Col: T(1), a(2), b(3), \t(4-8) -> next char at 9.
        runner.assert_cursor_position(1, 9)
        
        # 4. Move to before \t
        runner.send_keys('\x1b[D') # Left
        # Should be at index 3 (after 'b')
        # Col: T(1), a(2), b(3) -> next char at 4.
        runner.assert_cursor_position(1, 4)
        
        # 5. Move to start
        runner.send_keys('\x1b[D', count=3) # Left
        runner.assert_cursor_position(1, 1)
        
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_tabs_positioning()
    print("test_tabs passed!")
