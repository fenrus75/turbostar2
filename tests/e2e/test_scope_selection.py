import time
from turbostar_runner import TurbostarRunner

def test_scope_selection():
    runner = TurbostarRunner()
    try:
        runner.start()
        time.sleep(0.5)
        
        # 1. Type some nested code
        runner.send_keys("void outer() {\n")
        runner.send_keys("    if (cond) {\n")
        runner.send_keys("        inner_code();\n")
        runner.send_keys("    }\n")
        runner.send_keys("}")
        
        # 2. Move to middle of inner code
        # "        inner_code();" (Line 3)
        runner.send_keys('\x0b' + 'u') # Top
        runner.send_keys('\x1b[B', count=2) # Down 2 to line 3
        runner.send_keys('\x1b[C', count=10) # Right
        runner.assert_cursor_position(3, 11)
        
        # 3. Select inner scope via ^K [
        runner.send_keys('\x0b' + '[')
        time.sleep(0.5)
        
        # Verify selection markers (1-based)
        # Inner scope: "    if (cond) {" to "    }"
        # Start: 2:15, End: 4:6
        try:
            runner.assert_selection_is(2, 15, 4, 6)
        except AssertionError as e:
            print(f"Log contents:\n{runner.get_log()}")
            raise e
        
        runner.send_keys('\x0b' + 'y') # ^K Y (Delete block)
        time.sleep(1.0) # Wait longer for UI update
        
        try:
            runner.assert_text_not_on_screen("inner_code")
            runner.assert_text_on_screen("if (cond)")
            runner.assert_text_on_screen("void outer()")
        except AssertionError as e:
            print(f"Log contents:\n{runner.get_log()}")
            raise e

        # 4. Undo and try ^K {
        runner.send_keys('\x1f') # Undo
        time.sleep(1.0)
        runner.assert_text_on_screen("inner_code")
        
        # Select outer scope
        # Move to line 5 "}"
        runner.send_keys('\x0b' + 'v') # Bottom
        runner.send_keys('\x1b[D')   # Be ON the }
        runner.send_keys('\x0b' + '{')
        time.sleep(0.5)
        # Outer scope: 1:14 to 5:2
        try:
            runner.assert_selection_is(1, 14, 5, 2)
        except AssertionError as e:
            print(f"Log contents:\n{runner.get_log()}")
            raise e
        
        runner.send_keys('\x0b' + 'y') # Delete block
        time.sleep(0.5)
        
        # Braces and content should be gone, but "void outer()" remains
        try:
            runner.assert_text_not_on_screen("{")
            runner.assert_text_not_on_screen("inner_code")
            runner.assert_text_on_screen("void outer()")
        except AssertionError as e:
            print(f"Log contents:\n{runner.get_log()}")
            raise e

        runner.send_keys('\x0b' + 'q') # Ctrl-C
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_scope_selection()
    print("test_scope_selection passed!")
