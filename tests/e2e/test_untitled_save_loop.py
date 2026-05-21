import time
from turbostar_runner import TurbostarRunner

def test_untitled_save_loop():
    runner = TurbostarRunner()
    
    try:
        runner.start()
        
        # 1. Type to make the untitled document dirty
        runner.send_keys("Dirty text")
        runner.assert_text_on_screen("untitled*")
        
        # 2. Try to close the window
        runner.send_ctrlk('q') # Quit triggers close
        
        # 3. Assert save prompt appears
        runner.assert_text_on_screen("Save changes to untitled.txt?", timeout=2.0)
        
        # 4. Press 'S' to Save
        runner.send_keys('\x1b' + 's')
        
        # 5. It should prompt for Save As because it's untitled
        runner.assert_text_on_screen("Save File As", timeout=2.0)
        
        # 6. Let's cancel the Save As dialog
        runner.send_keys('\x1b' + 'c') # ESC to cancel Save As
        
        # 7. It should go back to the editor, NOT loop back to the save prompt
        runner.assert_text_not_on_screen("Save changes to untitled.txt?", timeout=2.0)
        runner.assert_text_not_on_screen("Save File As", timeout=2.0)
        
        # We should still be running and see our text
        runner.assert_text_on_screen("Dirty text")
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_untitled_save_loop()
    print("test_untitled_save_loop passed!")
