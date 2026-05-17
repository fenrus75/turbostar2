from turbostar_runner import TurbostarRunner
import time

def test_block_delete():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Setup multi-line text
        runner.send_keys("Line A\nLine B\nLine C")
        runner.assert_text_on_screen("Line A")
        runner.assert_text_on_screen("Line B")
        runner.assert_text_on_screen("Line C")
        
        # 2. Mark Selection: Start of "B" to Start of "C"
        # Move to line 2, start
        runner.move_cursor_to_line(2)
        runner.send_keys('\x01')    # Ctrl-A (Move to BOL)
        runner.assert_cursor_position(2, 1)
        runner.send_ctrlk('b') # ^KB
        
        # Move to line 3, start
        runner.move_cursor_to_line(3)
        runner.send_keys('\x01')    # Ctrl-A (Move to BOL)
        runner.assert_cursor_position(3, 1)
        runner.send_ctrlk('k') # ^KK
        
        # 3. Delete Block (^KY)
        runner.send_ctrlk('y') # ^KY
        
        runner.assert_content_is('tests/data/block_delete_golden.txt')
        
        # Cursor should be at 2:1
        runner.assert_cursor_position(2, 1)
        
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_block_delete()
    print("test_block_delete passed!")
