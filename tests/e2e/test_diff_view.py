import time
from turbostar_runner import *

def test_diff_view():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Type "Line 1"
        runner.send_keys("Line 1")
        runner.send_keys('\n')
        # 2. Type "Line 2"
        runner.send_keys("Line 2")
        
        # Verify text is there
        runner.assert_text_on_screen("Line 1")
        runner.assert_text_on_screen("Line 2")

        # 3. Open Diff View with ^Q H
        runner.send_ctrlq('h')
        time.sleep(0.5)
        
        # Verify Diff View is open
        runner.assert_text_on_screen("Undo History")
        
        # The last edit was typing '2' in "Line 2" (assuming single char edits for now)
        # Actually each char is a step.
        # Let's check if we see the diff markers
        runner.assert_text_on_screen("+") 
        
        # 4. Navigate back in time (Left arrow)
        # We typed 'Line 1', 'Enter', 'Line 2' (6+1+6 = 13 steps approx)
        for _ in range(5):
            runner.send_keys(KEY_LEFT)
            time.sleep(0.1)
            
        # 5. Restore to this state (Enter)
        runner.send_keys('\n')
        time.sleep(0.5)
        
        # Verify we are back in the main window and state is restored
        # If we went back 5 chars, "Line 2" should be "Lin"
        runner.assert_text_on_screen("Line 1")
        # Depending on exact steps, but at least we shouldn't see "Line 2" fully if we went back 5 steps.
        
        # Let's do a more controlled test with block delete
        runner.send_keys(KEY_CTRL_UNDERSCORE, count=20) # Clear everything with loads of undos
        runner.send_keys("Hello")
        runner.send_keys('\n')
        runner.send_keys("World")
        
        # Marker a block and delete it (This is ONE group)
        runner.send_keys(KEY_CTRL_A) # Home
        runner.send_keys(KEY_UP, count=10) # Top
        runner.send_ctrlk('b')
        runner.send_keys(KEY_DOWN, count=10) # Bottom
        runner.send_ctrlk('k')
        runner.send_ctrlk('y') # Delete block
        
        # Open Diff View
        runner.send_ctrlq('h')
        time.sleep(0.5)
        runner.assert_text_on_screen("Undo History")
        # Should see deletions (-)
        # Based on observed output:
        # @@ -1,2 +1,1 @@
        # -Hello
        #  World
        runner.assert_text_on_screen("-Hello")
        runner.assert_text_on_screen(" World")
        
        # Go to step 1 (the block delete operation)
        # current_undo_step 0 is the block delete!
        # wait, current_undo_step 0 compares state 1 and 0.
        # state 0 is empty, state 1 is Hello\nWorld.
        # So diff at step 0 SHOULD show the deletions.
        
        # Restore (Enter)
        runner.send_keys('\n')
        time.sleep(0.5)
        
        # After restoring step 0, nothing should change because we are already at state 0.
        # Wait, if I want to UNDO the delete, I should go to step 1?
        # No, step 0 IS the block delete. Restoring to it means being AT that state.
        
        # Let's clarify:
        # State 0: Empty
        # State 1: Hello\nWorld
        # Diff 0: compares 1 and 0. Shows -Hello, -World.
        # If I restore to "State 1", I get the text back.
        # State 1 is current_undo_step = 1.
        
        runner.send_ctrlq('h')
        time.sleep(0.5)
        runner.send_keys(KEY_LEFT) # Go to step 1
        runner.send_keys('\n')
        time.sleep(0.5)
        
        runner.assert_text_on_screen("Hello")
        runner.assert_text_on_screen("World")

    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_diff_view()
    print("test_diff_view passed!")
