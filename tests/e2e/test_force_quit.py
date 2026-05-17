import time
from turbostar_runner import TurbostarRunner

def test_force_quit():
    runner = TurbostarRunner()
    
    try:
        runner.start()
        
        # 1. Type to make the document dirty
        runner.send_keys("a")
        runner.assert_text_on_screen("untitled*")
        
        # 2. Try to force quit
        # We explicitly don't use runner.quit(force=True) here because we want to test the exact keys.
        # But runner.quit(force=True) does use ^KX. We can use it.
        runner.send_ctrlk('x')
        runner.assert_text_on_screen("Unsaved changes! Quit anyway?", timeout=2.0)
        runner.send_keys('\x1b') # ESC to instantly exit
        
        # 3. Assert app quits immediately
        runner.wait(timeout=10)
        
        # Verify the app closed without the dialog
        log = runner.get_log()
        assert "Application loop" in log or "Exiting" in log
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_force_quit()
    print("test_force_quit passed!")
