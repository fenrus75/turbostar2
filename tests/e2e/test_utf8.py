from turbostar_runner import TurbostarRunner
import time

def test_utf8_typing():
    runner = TurbostarRunner()
    try:
        runner.start()
        time.sleep(0.5)
        
        # '€' is U+20AC, UTF-8: E2 82 AC
        utf8_char = "€"
        runner.send_keys(utf8_char)
        
        time.sleep(0.5)
        
        # 1. Verify character is on screen
        runner.assert_text_on_screen(utf8_char)
        
        # 2. Verify cursor position (logical char pos)
        # It should be at 1:2 (moved 1 logical character)
        runner.assert_cursor_position(1, 2)
        
        # 3. Backspace it
        runner.send_keys('\x7f')
        
        time.sleep(0.5)
        
        # 4. Verify it's gone
        # Screen should be empty (or at least no '€')
        display = "".join(runner.screen.display)
        assert utf8_char not in display
        
        # 5. Verify cursor back at 1:1
        runner.assert_cursor_position(1, 1)
        
        runner.send_keys('\x0b' + 'q') # Ctrl-C
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_utf8_typing()
    print("test_utf8_typing passed!")
