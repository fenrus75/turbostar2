from turbostar_runner import *
import time

def test_k_block_help_dynamic():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # 1. Send Ctrl-K and check initial help
        # Note: initially, the document is NOT modified and there is NO active selection
        runner.send_raw_keys(b'\x0b') # Ctrl-K
        
        # Wait for status bar to show "K-Block:"
        runner.assert_text_on_screen("K-Block:", timeout=2.0)
        
        # "S:Save" should NOT be visible since the document is not modified
        runner.assert_text_not_on_screen("S:Save")
        
        # "Y:Del" should NOT be visible since there is no active selection
        runner.assert_text_not_on_screen("Y:Del")
        
        # 2. Cancel Ctrl-K mode by pressing Escape
        runner.send_keys(KEY_ESC)
        runner.assert_text_not_on_screen("K-Block:", timeout=2.0)
        
        # 3. Type some characters to modify the document
        runner.send_keys("Hello World")
        
        # 4. Now send Ctrl-K and check that S:Save is visible
        runner.send_raw_keys(b'\x0b') # Ctrl-K
        runner.assert_text_on_screen("K-Block:", timeout=2.0)
        runner.assert_text_on_screen("S:Save", timeout=2.0)
        
        # But selection-based helpers (Del, Copy, Move, Hide) should still be hidden
        runner.assert_text_not_on_screen("Y:Del")
        runner.assert_text_not_on_screen("C:Copy")
        
        # 5. Cancel Ctrl-K mode again
        runner.send_keys(KEY_ESC)
        runner.assert_text_not_on_screen("K-Block:", timeout=2.0)
        
        # 6. Select text to activate selection
        # We can set Selection Begin at top of file, move right, set Selection End
        runner.send_ctrlk('u') # Go to top of file
        runner.send_ctrlk('b') # Set Begin
        
        runner.send_keys(KEY_RIGHT)
        runner.send_keys(KEY_RIGHT)
        runner.send_ctrlk('k') # Set End
        
        # Now we have an active selection!
        # Send Ctrl-K and verify that selection-based helpers are shown
        runner.send_raw_keys(b'\x0b') # Ctrl-K
        runner.assert_text_on_screen("K-Block:", timeout=2.0)
        runner.assert_text_on_screen("Y:Del", timeout=2.0)
        runner.assert_text_on_screen("C:Copy", timeout=2.0)
        runner.assert_text_on_screen("M:Move", timeout=2.0)
        runner.assert_text_on_screen("H:Hide", timeout=2.0)
        
        # 7. Quit the application
        runner.send_keys('q') # Quit from K-Block
        # It's dirty, so it'll prompt. Let's quit without saving.
        # Send Alt+d (ESC + d) to discard
        runner.send_keys(KEY_ESC + 'd')
        runner.wait(timeout=5)

    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_k_block_help_dynamic()
    print("test_k_block_help_dynamic passed!")
