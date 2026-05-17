import time
from turbostar_runner import TurbostarRunner

def test_close_dirty():
    runner = TurbostarRunner()
    
    try:
        runner.start()
        
        # 1. Type to make the document dirty
        runner.send_keys("Dirty text")
        runner.assert_text_on_screen("untitled*")
        
        # 2. Try to close the window
        runner.send_ctrlk('q') # Quit triggers close
        
        # 3. Assert prompt appears
        runner.assert_text_on_screen("Save changes to untitled.txt?", timeout=2.0)
        
        # 4. Press 'C' to Cancel
        runner.send_keys('c')
        runner.assert_text_not_on_screen("Save changes to untitled.txt?", timeout=2.0)
        
        # We should still be running
        runner.assert_text_on_screen("Dirty text")
        
        # 5. Try to close again, this time discard
        runner.send_ctrlk('q')
        runner.assert_text_on_screen("Save changes to untitled.txt?", timeout=2.0)
        runner.send_keys('d') # Discard
        
        # It should discard and quit
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_close_dirty()
    print("test_close_dirty passed!")
