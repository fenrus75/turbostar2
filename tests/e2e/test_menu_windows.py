import time
from turbostar_runner import TurbostarRunner

def test_window_menu_switching():
    runner = TurbostarRunner()
    try:
        runner.start()
        time.sleep(0.5)
        
        # 1. Open a second window
        runner.send_keys('\x1bf') # Alt-F
        time.sleep(0.2)
        runner.send_keys('n') # New window
        time.sleep(0.5)
        
        # 2. Check "Window" menu content
        runner.send_keys('\x1bw') # Alt-W
        time.sleep(0.5)
        
        # Should have two "noname.txt" items (or similar)
        # Plus "Close" and a separator at the top.
        # 3. Select the second item (index 1)
        # Use Down key twice then Enter
        runner.send_keys('\x1b[B', count=2) # Down x 2
        runner.send_keys('\n')
        
        time.sleep(0.5)
        
        # 4. Verify log for correct window selection
        # Note: activate_window logs "Selecting window: N" via dispatcher
        log = runner.get_log()
        if "Selecting window: 1" not in log:
            print(f"FAILED to switch to window 1. Log:\n{log}")
            # If it's the bug, it might say "Selecting window: 0"
            raise AssertionError("Window menu failed to select window 1")

        # 5. Select the first window item (index 0)
        runner.send_keys('\x1bw') # Alt-W
        time.sleep(0.5)
        # Menu starts at "Close". Window 0 is one Down away (skipping separator).
        runner.send_keys('\x1b[B') # Down x 1
        runner.send_keys('\n')
        time.sleep(0.5)
        
        log = runner.get_log()
        # Find the LAST "Selecting window" message
        last_selecting = [line for line in log.splitlines() if "Selecting window:" in line][-1]
        assert "Selecting window: 0" in last_selecting

        runner.send_keys('\x0b' + 'q')
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_window_menu_switching()
    print("test_window_menu_switching passed!")
