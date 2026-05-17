from turbostar_runner import TurbostarRunner
import time

def test_block_delete():
    runner = TurbostarRunner()
    try:
        runner.start()
        time.sleep(0.5)
        
        # 1. Setup multi-line text
        runner.send_keys("Line A\nLine B\nLine C")
        runner.assert_text_on_screen("Line A")
        runner.assert_text_on_screen("Line B")
        runner.assert_text_on_screen("Line C")
        
        # 2. Mark Selection: Start of "B" to Start of "C"
        # Move to line 2, start
        runner.send_keys('\x1b[A') # Up (to line 2)
        runner.send_keys('\x01')    # Ctrl-A (Move to BOL)
        runner.assert_cursor_position(2, 1)
        runner.send_keys('\x0b' + 'b') # ^KB
        
        # Move to line 3, start
        runner.send_keys('\x1b[B') # Down
        runner.assert_cursor_position(3, 1)
        runner.send_keys('\x0b' + 'k') # ^KK
        
        # 3. Delete Block (^KY)
        runner.send_keys('\x0b' + 'y') # ^KY
        
        time.sleep(0.5)
        
        # "Line B" should be gone. "Line C" should move up.
        # Wait, if we delete [2:1 to 3:1), we delete "Line B\n".
        # Screen should have "Line A" and "Line C".
        runner.assert_text_on_screen("Line A")
        runner.assert_text_on_screen("Line C")
        display = "\n".join(runner.screen.display)
        assert "Line B" not in display
        
        # Cursor should be at 2:1
        runner.assert_cursor_position(2, 1)
        
        runner.send_keys('\x0b' + 'q') # Ctrl-C
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_block_delete()
    print("test_block_delete passed!")
