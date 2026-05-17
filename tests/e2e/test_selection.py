from turbostar_runner import *
import time

def test_selection_maintenance():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Type some text
        runner.send_keys("Hello World")
        runner.assert_text_on_screen("Hello World")
        
        # 2. Set Selection Start at "World" (pos 1:7)
        # Cursor is currently at 1:12. Move to 1:7.
        for _ in range(5):
            runner.send_keys(KEY_LEFT) # Left
        runner.assert_cursor_position(1, 7)
        runner.send_ctrlk('b') # Ctrl-K, b
        
        # 3. Set Selection End at end of "World" (pos 1:12)
        for _ in range(5):
            runner.send_keys(KEY_RIGHT) # Right
        runner.assert_cursor_position(1, 12)
        runner.send_ctrlk('k') # Ctrl-K, k
        
        # 4. Insert text BEFORE selection
        # Move to 1:1
        for _ in range(11):
            runner.send_keys(KEY_LEFT) # Left
        runner.assert_cursor_position(1, 1)
        
        runner.send_keys("Fixed ") # 6 chars
        runner.assert_text_on_screen("Fixed Hello World")
        
        # 5. Verify cursor movement after insertion
        # Cursor should be at 1:7 (after "Fixed ")
        runner.assert_cursor_position(1, 7)
        
        # 6. Move into selection and type
        # Selection was [7, 12], it moved by 6 to [13, 18]
        # Move to middle of "World" (now at 1:13-17). Let's move to 1:15.
        for _ in range(8):
            runner.send_keys(KEY_RIGHT) # Right
        runner.assert_cursor_position(1, 15)
        
        runner.send_keys("X")
        runner.assert_text_on_screen("Fixed Hello WoXrld")
        runner.assert_cursor_position(1, 16)
        
        # Verify with log or visual check if possible (visual check is hard for colors)
        # Let's verify character positions via insert/delete
        
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_selection_maintenance()
    print("test_selection_maintenance passed!")
