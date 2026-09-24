from turbostar_runner import *
import time

def test_replace_functionality():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Setup text with banana
        content = "banana apple banana cherry\nbanana grape"
        runner.send_keys(content)
        
        # Move to top
        runner.send_ctrlk('u')
        runner.assert_cursor_position(1, 1)
        
        # 2. Test Replace dialog Change All via ^QA
        runner.send_ctrlq('a')
        runner.assert_text_on_screen("Replace", timeout=2.0)
        runner.send_keys("banana\n")
        time.sleep(0.3)
        runner.send_keys(KEY_CTRL_Y) # Clear pre-filled
        runner.send_keys("orange")
        
        # Now let's trigger Change All (hotkey 'a' -> Alt-A)
        runner.send_keys('\x1b' + 'a')
        time.sleep(0.5)
        
        # Move to top to verify replacements
        runner.send_ctrlk('u')
        
        # First, search for "orange" to set it as current
        runner.send_ctrlk('f')
        runner.send_keys("orange\n\n")
        runner.assert_cursor_position(1, 1, timeout=1.5)
        
        # Search next orange
        runner.send_keys(KEY_CTRL_L)
        runner.assert_cursor_position(1, 14, timeout=1.5)
        
        # Search next orange
        runner.send_keys(KEY_CTRL_L)
        runner.assert_cursor_position(2, 1, timeout=1.5)

    finally:
        runner.cleanup()

def test_prompt_replace_consecutive_and_skip():
    """Verify interactive prompt replace:
       1) Enter in 'Text to find' advances to 'Replace with'
       2) 'y' replaces and 'n' skips
       3) Consecutive occurrences ('foofoo') are both found and replaced
    """
    runner = TurbostarRunner()
    try:
        runner.start()
        # Input text with consecutive 'foo' and a second line
        runner.send_keys("foofoo test foo\nfoo bar")
        runner.send_ctrlk('u')
        runner.assert_cursor_position(1, 1)

        # Open Replace dialog via ^QA
        runner.send_ctrlq('a')
        runner.assert_text_on_screen("Replace", timeout=2.0)

        # Type 'foo' in 'Text to find' and press Enter.
        # Enter should advance focus to 'Replace with' without closing the dialog.
        runner.send_keys("foo\n")
        time.sleep(0.3)
        runner.assert_text_on_screen("Replace with", timeout=1.0)
        runner.assert_text_on_screen("Replace", timeout=1.0) # Dialog still open!

        # Type replacement 'baz' and press Enter to start prompt-based replace
        runner.send_keys(KEY_CTRL_Y)
        runner.send_keys("baz\n")
        time.sleep(0.5)

        # Prompt should appear
        runner.assert_text_on_screen("Replace? (Y)es / (N)o / (A)ll / (Q)uit", timeout=2.0)

        # First match is the first 'foo' in 'foofoo'. Press 'y' to replace.
        runner.send_keys('y')
        time.sleep(0.3)

        # Next match is immediately adjacent second 'foo' in 'bazfoo'. Press 'y' to replace.
        runner.assert_text_on_screen("Replace? (Y)es / (N)o / (A)ll / (Q)uit", timeout=2.0)
        runner.send_keys('y')
        time.sleep(0.3)

        # Next match is third 'foo' on line 1. Press 'n' to skip it.
        runner.assert_text_on_screen("Replace? (Y)es / (N)o / (A)ll / (Q)uit", timeout=2.0)
        runner.send_keys('n')
        time.sleep(0.3)

        # Next match is 'foo' on line 2. Press 'y' to replace.
        runner.assert_text_on_screen("Replace? (Y)es / (N)o / (A)ll / (Q)uit", timeout=2.0)
        runner.send_keys('y')
        time.sleep(0.5)

        # Prompt should complete
        runner.assert_text_on_screen("Search/replace complete.", timeout=2.0)

        # Verify screen content:
        # Line 1 should be "bazbaz test foo"
        # Line 2 should be "baz bar"
        runner._read_output()
        l1 = runner.screen.display[2]
        l2 = runner.screen.display[3]
        assert "bazbaz test foo" in l1, f"Expected 'bazbaz test foo', got: {l1}"
        assert "baz bar" in l2, f"Expected 'baz bar', got: {l2}"

    finally:
        runner.cleanup()

def test_replace_selected_scope():
    """Verify that selecting a block scopes replace to the selection."""
    runner = TurbostarRunner()
    try:
        runner.start()
        content = "item item\nitem item\nitem item\n"
        runner.send_keys(content)
        runner.send_ctrlk('u')

        # Select line 2 only
        runner.send_keys(KEY_DOWN)
        runner.send_ctrlk('b')
        runner.send_keys(KEY_RIGHT, count=9)
        runner.send_ctrlk('k')

        # Open Replace dialog via ^QA
        runner.send_ctrlq('a')
        runner.assert_text_on_screen("Replace", timeout=2.0)

        # Type 'item' and Enter (moves to replacement)
        runner.send_keys("item\n")
        time.sleep(0.3)
        runner.send_keys(KEY_CTRL_Y)
        runner.send_keys("DONE")

        # Change all (Alt-A)
        runner.send_keys('\x1b' + 'a')
        time.sleep(0.5)

        # Verify only line 2 was changed
        runner._read_output()
        l1 = runner.screen.display[2]
        l2 = runner.screen.display[3]
        l3 = runner.screen.display[4]
        assert "item item" in l1, f"Line 1 should be unchanged, got: {l1}"
        assert "DONE DONE" in l2, f"Line 2 should be 'DONE DONE', got: {l2}"
        assert "item item" in l3, f"Line 3 should be unchanged, got: {l3}"

    finally:
        runner.cleanup()

def test_joe_search_replace_flow():
    """Verify JOE-compatible keystroke flow via status bar:
       ^KF -> search term -> Enter -> r -> Enter -> Replace with: -> replacement -> Enter
       -> Replace? (Y)es / (N)o / (A)ll / (Q)uit
       Ensures no modal dialog popup appears.
    """
    runner = TurbostarRunner()
    try:
        runner.start()
        content = "apple orange apple lemon apple\n"
        runner.send_keys(content)
        runner.send_ctrlk('u')
        runner.assert_cursor_position(1, 1)

        # 1. ^KF
        runner.send_ctrlk('f')
        runner.assert_text_on_screen("Search for:", timeout=2.0)

        # 2. Type search query
        runner.send_keys("apple\n")
        runner.assert_text_on_screen("Options (I R B K):", timeout=2.0)

        # 3. Type 'r' for replace
        runner.send_keys("r\n")
        time.sleep(0.3)

        # 4. Status bar must ask for replacement without opening dialog
        runner.assert_text_on_screen("Replace with:", timeout=2.0)

        # 5. Type replacement 'pear'
        runner.send_keys("pear\n")
        time.sleep(0.3)

        # 6. Prompt mode
        runner.assert_text_on_screen("Replace? (Y)es / (N)o / (A)ll / (Q)uit", timeout=2.0)

        # Match 1: Replace with 'y'
        runner.send_keys('y')
        time.sleep(0.3)

        # Match 2: Skip with 'n'
        runner.assert_text_on_screen("Replace? (Y)es / (N)o / (A)ll / (Q)uit", timeout=2.0)
        runner.send_keys('n')
        time.sleep(0.3)

        # Match 3: Replace with 'y'
        runner.assert_text_on_screen("Replace? (Y)es / (N)o / (A)ll / (Q)uit", timeout=2.0)
        runner.send_keys('y')
        time.sleep(0.5)

        runner.assert_text_on_screen("Search/replace complete.", timeout=2.0)

        # Verify document text
        runner._read_output()
        l1 = runner.screen.display[2]
        assert "pear orange apple lemon pear" in l1, f"Expected 'pear orange apple lemon pear', got: {l1}"

    finally:
        runner.cleanup()

def test_joe_replace_all_remaining():
    """Verify that 'a' in JOE replace prompt replaces all REMAINING occurrences
       without re-replacing previously skipped occurrences.
    """
    runner = TurbostarRunner()
    try:
        runner.start()
        content = "cat cat cat cat\n"
        runner.send_keys(content)
        runner.send_ctrlk('u')
        runner.assert_cursor_position(1, 1)

        runner.send_ctrlk('f')
        runner.send_keys("cat\n")
        runner.send_keys("r\n")
        time.sleep(0.3)
        runner.assert_text_on_screen("Replace with:", timeout=2.0)

        runner.send_keys("dog\n")
        time.sleep(0.3)
        runner.assert_text_on_screen("Replace? (Y)es / (N)o / (A)ll / (Q)uit", timeout=2.0)

        # Skip first 'cat'
        runner.send_keys('n')
        time.sleep(0.3)
        runner.assert_text_on_screen("Replace? (Y)es / (N)o / (A)ll / (Q)uit", timeout=2.0)

        # Press 'a' on second 'cat' to replace all remaining
        runner.send_keys('a')
        time.sleep(0.5)

        # First cat must remain 'cat', last 3 must become 'dog'
        runner._read_output()
        l1 = runner.screen.display[2]
        assert "cat dog dog dog" in l1, f"Expected 'cat dog dog dog', got: {l1}"

    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_joe_search_replace_flow()
    print("test_joe_search_replace_flow passed!")
    test_joe_replace_all_remaining()
    print("test_joe_replace_all_remaining passed!")
    test_replace_functionality()
    print("test_replace_functionality passed!")
    test_prompt_replace_consecutive_and_skip()
    print("test_prompt_replace_consecutive_and_skip passed!")
    test_replace_selected_scope()
    print("test_replace_selected_scope passed!")
