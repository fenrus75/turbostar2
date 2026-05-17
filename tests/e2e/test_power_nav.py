from turbostar_runner import TurbostarRunner
import time

def test_power_navigation():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Load document with multiple lines
        runner.insert_file('tests/data/power_nav_start.txt')
        
        # 2. Test ^KU (Top of File)
        runner.send_ctrlk('u') # ^KU
        runner.assert_cursor_position(1, 1)
        
        # 3. Test ^KV (End of File)
        runner.send_ctrlk('v') # ^KV
        runner.assert_cursor_position(30, 19) # "WordA WordB Line30" is 18 chars, so pos 19
        
        # 4. Test ^U (Page Up)
        # Assuming content height is 20 (24 lines total - menu - status - borders)
        runner.send_keys('\x15') # ^U
        runner.assert_cursor_position(10, 19)
        
        # 5. Test ^V (Page Down)
        runner.send_keys('\x16') # ^V
        runner.assert_cursor_position(30, 19)
        
        # 6. Test ^X (Next Word)
        # Move to top first
        runner.send_ctrlk('u')
        runner.assert_cursor_position(1, 1)
        
        runner.send_keys('\x18') # ^X
        # "WordA WordB Line1"
        # From start, skip "WordA", should be at "WordB" (pos 7)
        runner.assert_cursor_position(1, 7)
        
        runner.send_keys('\x18') # ^X
        # Skip "WordB", should be at "Line1" (pos 13)
        runner.assert_cursor_position(1, 13)
        
        # 7. Test ^Z (Prev Word)
        runner.send_keys('\x1a') # ^Z
        # Back to "WordB" (pos 7)
        runner.assert_cursor_position(1, 7)
        
        runner.send_keys('\x1a') # ^Z
        # Back to "WordA" (pos 1)
        runner.assert_cursor_position(1, 1)
        
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_power_navigation()
    print("test_power_navigation passed!")
