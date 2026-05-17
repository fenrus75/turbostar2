import time
from turbostar_runner import TurbostarRunner

def test_ghost_x():
    runner = TurbostarRunner()
    try:
        runner.start()
        time.sleep(0.5)
        
        # 1. Type the scenario
        # Line 1: 20 chars
        runner.send_keys("12345678901234567890\n")
        # Line 2: Empty
        runner.send_keys("\n")
        # Line 3: 20 chars
        runner.send_keys("abcdefghijklmnopqrst\n")
        
        # 2. Move to top, X = 10
        runner.send_ctrlk('u') # ^K U (Top)
        runner.send_keys('\x1b[C', count=10) # Right 10 times
        
        runner.assert_cursor_position(1, 11)
        
        # 3. Move down to empty line
        runner.send_keys('\x1b[B') # Down
        runner.assert_cursor_position(2, 1) # Display col 1, index 0
        
        # 4. Move down again to line 3
        runner.send_keys('\x1b[B') # Down
        # Should be at X=10 (display col 11)
        runner.assert_cursor_position(3, 11)
        
        # 5. Move right, it should reset the target
        runner.send_keys('\x1b[C') # Right
        runner.assert_cursor_position(3, 12)
        
        runner.send_keys('\x1b[A', count=2) # Up 2 to line 1
        runner.assert_cursor_position(1, 12) # Should be 12 now

        runner.send_ctrlk('q') # Ctrl-C
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_ghost_x()
    print("test_ghost_x passed!")
