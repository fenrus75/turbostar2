from turbostar_runner import TurbostarRunner
import time

def test_block_copy_move():
    runner = TurbostarRunner()
    try:
        runner.start()
        time.sleep(0.5)
        
        # 1. Setup text
        runner.send_keys("Start\nTarget\n[Block]\nEnd")
        runner.assert_text_on_screen("Target")
        runner.assert_text_on_screen("[Block]")
        
        # 2. Mark Block around "[Block]" (line 3)
        # Move to line 3, start
        runner.send_ctrlk('u') # ^KU (top)
        runner.send_keys('\x16')       # ^V (page down, but small doc so just moves)
        # Wait, ^V moves by page height. Let's just use Down Down.
        runner.send_ctrlk('u')
        runner.send_keys('\x1b[B') # Down
        runner.send_keys('\x1b[B') # Down (line 3)
        runner.assert_cursor_position(3, 1)
        runner.send_ctrlk('b') # ^KB
        runner.send_keys('\x05')       # ^E (end of line 3)
        runner.assert_cursor_position(3, 8)
        runner.send_ctrlk('k') # ^KK
        
        # 3. Copy Block (^KC) to after "Target" (line 2)
        # Move to line 2, end
        runner.send_keys('\x1b[A') # Up (line 2)
        runner.send_keys('\x05')    # ^E
        runner.assert_cursor_position(2, 7)
        runner.send_ctrlk('c') # ^KC
        
        time.sleep(0.5)
        # Screen should now have "[Block]" twice
        # Line 1: Start
        # Line 2: Target[Block]
        # Line 3: [Block]
        # Line 4: End
        runner.assert_text_on_screen("Target[Block]")
        
        # 4. Move Block (^KM) to line 1, end
        # Current block is at line 2:7 to line 2:14 (since Target is 6 chars)
        # Move cursor to line 1, end
        runner.send_keys('\x1b[A') # Up (line 1)
        runner.send_keys('\x05')    # ^E
        runner.assert_cursor_position(1, 6)
        runner.send_ctrlk('m') # ^KM
        
        time.sleep(0.5)
        # Result:
        # Line 1: Start[Block]
        # Line 2: Target
        # Line 3: [Block]
        # Line 4: End
        runner.assert_text_on_screen("Start[Block]")
        # Verify it's gone from line 2
        display = "\n".join(runner.screen.display)
        assert "Target[Block]" not in display
        
        runner.send_ctrlk('q') # Ctrl-C
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_block_copy_move()
    print("test_block_copy_move passed!")
