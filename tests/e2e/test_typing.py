from turbostar_runner import TurbostarRunner
import time

def test_basic_typing():
    runner = TurbostarRunner()
    try:
        runner.start()
        # Wait for initialization
        time.sleep(0.5)
        
        # Type "Hello TurboStar"
        test_string = "Hello TurboStar"
        runner.send_keys(test_string)
        
        # Wait for processing
        time.sleep(0.5)
        
        # Verify text is on screen
        runner.assert_text_on_screen(test_string)
        
        # Verify cursor position (it should have moved 15 chars)
        # 1:1 is start, so it should be at 1:16
        runner.assert_cursor_position(1, 16)
        
        runner.send_ctrlk('q') # Ctrl-C to quit
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_basic_typing()
    print("test_typing passed!")
