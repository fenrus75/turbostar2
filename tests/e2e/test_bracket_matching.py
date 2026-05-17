import time
from turbostar_runner import *

def test_bracket_matching():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Type some code with nested brackets
        runner.send_keys("if (a == (b + c)) {\n")
        runner.send_keys("    d[i] = {1, 2, 3};\n")
        runner.send_keys("}")
        
        # 2. Test matching ()
        # Move to the first '(' at 1:4
        runner.send_ctrlk('u') # Top
        for _ in range(3): runner.send_keys(KEY_RIGHT) # Right to 1:4
        runner.assert_cursor_position(1, 4)
        
        # Press ^G (Matching bracket)
        runner.send_keys(KEY_CTRL_G) # Ctrl-G

        # "if (a == (b + c)) {"
        #  12345678901234567
        # The matching ')' is at 17
        runner.assert_cursor_position(1, 17, timeout=1.5)
        
        # Press ^G again to go back
        runner.send_keys(KEY_CTRL_G)

        runner.assert_cursor_position(1, 4, timeout=1.5)

        # 3. Test matching []
        # Move to '[' at 2:6
        # "    d[i] = {1, 2, 3};"
        #  123456
        runner.send_keys(KEY_DOWN) # Down to line 2
        runner.send_keys(KEY_CTRL_A)   # Start of line
        for _ in range(5): runner.send_keys(KEY_RIGHT) # Right to 2:6
        runner.assert_cursor_position(2, 6)
        
        runner.send_keys(KEY_CTRL_G)

        # Should be at ']' at 2:8
        runner.assert_cursor_position(2, 8, timeout=1.5)
        
        # 4. Test matching {}
        # Move to '{' at 2:12
        # "    d[i] = {1, 2, 3};"
        #  123456789012
        for _ in range(4): runner.send_keys(KEY_RIGHT) # Right to 2:12
        runner.assert_cursor_position(2, 12)
        
        runner.send_keys(KEY_CTRL_G)

        # Should be at '}' at 2:20
        runner.assert_cursor_position(2, 20, timeout=1.5)
        
        # 5. Test multi-line matching {}
        # Move to '{' at 1:19
        runner.send_ctrlk('u')
        runner.send_keys(KEY_CTRL_E) # End of line 1
        runner.send_keys(KEY_LEFT) # Left once to be ON the '{'
        runner.assert_cursor_position(1, 19)
        
        runner.send_keys(KEY_CTRL_G)

        # Should be at '}' at 3:1
        runner.assert_cursor_position(3, 1, timeout=1.5)

        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_bracket_matching()
    print("test_bracket_matching passed!")
