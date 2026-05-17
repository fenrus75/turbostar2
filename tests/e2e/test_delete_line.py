from turbostar_runner import TurbostarRunner
import time

def test_delete_line():
    runner = TurbostarRunner()
    try:
        runner.start()
        # Type three lines
        runner.send_keys("Line 1\nLine 2\nLine 3")
        runner.assert_text_on_screen("Line 1")
        runner.assert_text_on_screen("Line 2")
        runner.assert_text_on_screen("Line 3")
        
        # Currently at 3:7. Move up to line 2.
        runner.send_keys('\x1b[A') # Up
        runner.assert_cursor_position(2, 7)
        
        # Press Ctrl-Y (ASCII 25)
        runner.send_keys('\x19') # \x19 is 25
        

        # "Line 2" should be gone, "Line 3" should move to line 2.
        runner.assert_text_on_screen("Line 1", timeout=1.5)
        runner.assert_text_on_screen("Line 3")
        
        # Verify "Line 2" is actually gone
        runner.assert_text_not_on_screen("Line 2")
        
        # Cursor should be at 2:1
        runner.assert_cursor_position(2, 1)
        
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_delete_line()
    print("test_delete_line passed!")
