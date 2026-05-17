import time
from turbostar_runner import TurbostarRunner

def test_scope_selection():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Load some nested code
        runner.insert_file('tests/data/scope_sel_start.txt')
        
        # 2. Move to middle of inner code
        # "        inner_code();" (Line 3)
        runner.send_ctrlk('u') # Top
        runner.send_keys('\x1b[B', count=2) # Down 2 to line 3
        runner.send_keys('\x1b[C', count=10) # Right
        runner.assert_cursor_position(3, 11)
        
        # 3. Select inner scope via ^K [
        runner.send_ctrlk('[')
        time.sleep(0.5)
        
        # Verify selection markers (1-based)
        # Inner scope: "    if (cond) {" to "    }"
        # Start: 2:15, End: 4:6
        try:
            runner.assert_selection_is(2, 15, 4, 6)
        except AssertionError as e:
            print(f"Log contents:\n{runner.get_log()}")
            raise e
        
        runner.send_ctrlk('y') # ^K Y (Delete block)
        time.sleep(0.3) # Wait longer for UI update
        
        runner.assert_content_is('tests/data/scope_sel_1.txt')

        # 4. Undo and try ^K {
        runner.send_keys('\x1f') # Undo

        runner.assert_text_on_screen("inner_code", timeout=2.0)
        
        # Select outer scope
        # Move to line 5 "}"
        runner.send_ctrlk('v') # Bottom
        runner.send_keys('\x1b[D')   # Be ON the }
        runner.send_ctrlk('{')
        time.sleep(0.5)
        # Outer scope: 1:14 to 5:2
        try:
            runner.assert_selection_is(1, 14, 5, 2)
        except AssertionError as e:
            print(f"Log contents:\n{runner.get_log()}")
            raise e
        
        runner.send_ctrlk('y') # Delete block
        runner.assert_content_is('tests/data/scope_sel_2.txt')

        runner.send_ctrlk('q') # Ctrl-C
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_scope_selection()
    print("test_scope_selection passed!")
