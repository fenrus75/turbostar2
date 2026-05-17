from turbostar_runner import TurbostarRunner
import time

def test_word_line_editing():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Test ^W (Delete Word Forward)
        runner.send_keys("One Two Three")
        runner.assert_cursor_position(1, 14)
        # Move to start of "Two"
        for _ in range(9):
            runner.send_keys('\x1b[D') # Left
        runner.assert_cursor_position(1, 5) # "One " is 4 chars, so "Two" starts at 5
        
        runner.send_keys('\x17') # ^W (23) deletes "Two "
        runner.assert_text_on_screen("One Three")
        runner.assert_cursor_position(1, 5)
        
        # 2. Test ^O (Delete Word Backward)
        # Currently at 1:5 (before "Three")
        # Move to end of "Three"
        for _ in range(5):
            runner.send_keys('\x1b[C') # Right
        runner.assert_cursor_position(1, 10)
        
        runner.send_keys('\x0f') # ^O (15) deletes "Three"
        runner.assert_text_on_screen("One ")
        runner.assert_cursor_position(1, 5)
        
        # 3. Test ^J (Delete to EOL)
        runner.send_keys("End Of Line")
        # "One End Of Line"
        # Move to before "Of"
        for _ in range(7):
            runner.send_keys('\x1b[D') # Left
        runner.assert_cursor_position(1, 9)
        
        runner.send_raw_keys('\x0a') # ^J (10) deletes "Of Line"
        runner.assert_text_on_screen("One End ")
        # Verify "Of Line" is gone
        runner.assert_text_not_on_screen("Of Line")
        
        # 4. Test Alt-O (Delete to BOL)
        # Currently at 1:9 (after "One End ")
        # Move to after "One "
        for _ in range(4):
            runner.send_keys('\x1b[D') # Left
        runner.assert_cursor_position(1, 5)
        
        runner.send_keys('\x1bo') # Alt-o (Esc o)
        runner.assert_text_on_screen("End ")
        # Verify "One " is gone
        runner.assert_text_not_on_screen("One ")
        runner.assert_cursor_position(1, 1)
        
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_word_line_editing()
    print("test_word_line_editing passed!")
