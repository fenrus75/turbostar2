from turbostar_runner import TurbostarRunner
import time

def test_search_functionality():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Setup multi-line text with repeating words
        content = "Search Alpha\nSearch Beta\nSearch Gamma"
        runner.send_keys(content)
        # Final cursor at 3:13
        
        # 2. Test ^KF (Status Bar Search)
        # Move to top
        runner.send_ctrlk('u')
        runner.assert_cursor_position(1, 1)
        
        # Start search for "Beta"
        runner.send_ctrlk('f')
        runner.send_keys("Beta\n\n")

        # Should be at start of "Beta" (2:8)
        runner.assert_cursor_position(2, 8, timeout=1.5)
        
        # 3. Test ^L (Repeat Search)
        # First, search for "Search" to set it as current
        runner.send_ctrlk('f')
        runner.send_keys("Search\n\n")

        # Found at 1:1? No, find_next starts from cursor+1.
        # If we were at 2:8, it starts at 2:9.
        # It should find "Search" at 3:1.
        runner.assert_cursor_position(3, 1, timeout=1.5)

        # Go to top
        runner.send_ctrlk('u')
        # Now ^L should find "Search" at 2:1 (since it starts at 1:2)
        runner.send_keys('\x0c') # ^L (12)
        runner.assert_cursor_position(2, 1)
        
        runner.send_keys('\x0c') # ^L
        runner.assert_cursor_position(3, 1)
        
        # 4. Test Menu Search (Dialog)
        runner.send_keys('\x1b' + 's') # Alt-S (Search menu)
        runner.send_keys('f')          # 'f' for Find...
        time.sleep(0.5)
        
        # Dialog is open, pre-filled with "Beta"
        # Clear and type "Gamma"
        runner.send_keys('\x7f', count=10)
        runner.send_keys("Gamma\n")

        # Should find "Gamma" at 3:8
        runner.assert_cursor_position(3, 8, timeout=1.5)
        
        runner.send_ctrlk('q')
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_search_functionality()
    print("test_search passed!")
