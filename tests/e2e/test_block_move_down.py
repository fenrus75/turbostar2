import time
from turbostar_runner import *

def test_block_move_down():
    runner = TurbostarRunner()
    
    try:
        runner.start()
        
        # 1. Load starting text
        runner.insert_file('tests/data/block_move_start.txt')
        
        # Start text is:
        # 1: Line 1
        # 2: Line 2
        # 3: Line 3
        # 4: 
        # 5: #
        # 6: ##
        
        # We want to move Line 2 and Line 3 down to be between # and ##.
        # This means selecting from 2:1 up to (but not including) 4:1.
        runner.move_cursor_to_line(2)
        runner.send_keys(KEY_CTRL_A) # Ctrl-A (Home)
        runner.send_ctrlk('b') # Selection begin
        
        runner.move_cursor_to_line(4)
        runner.send_keys(KEY_CTRL_A) # Ctrl-A (Home)
        runner.send_ctrlk('k') # Selection end
        
        runner.assert_selection_is(2, 1, 4, 1, timeout=2.0)
        
        # 2. Move cursor to the destination.
        # We want the block to end up between # (line 5) and ## (line 6).
        # In a normal editor, pasting/moving at line 6 puts the content BEFORE line 6.
        # So we move cursor to line 6 (the "##" line).
        runner.move_cursor_to_line(6)
        runner.send_keys(KEY_CTRL_A) # Ctrl-A (Home)
        
        # 3. Trigger Block Move (^KM)
        runner.send_ctrlk('m')
        
        # Clear selection so assert_content_is can save the whole file
        runner.send_ctrlk('h')
        
        # 4. Verify content matches golden
        runner.assert_content_is('tests/data/block_move_golden.txt')
        
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_block_move_down()
    print("test_block_move_down passed!")
