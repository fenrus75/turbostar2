import time
from turbostar_runner import TurbostarRunner

def test_search_autocomplete():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Type some text
        runner.send_keys("The quick brown fox jumps over the lazy dog\n")
        
        # Ensure we are at the top before searching
        runner.send_ctrlk('u') # ^K U (Top of file)
        runner.assert_cursor_position(1, 1)

        # 2. Perform initial search to populate history
        runner.send_ctrlk('f') # ^K F
        runner.send_keys("fox\n\n")
        time.sleep(0.5)
        
        try:
            runner.assert_cursor_position(1, 17) # Cursor at start of 'fox'
        except AssertionError as e:
            print(f"Log contents:\n{runner.get_log()}")
            raise e
        
        # 3. Perform second search to trigger autocomplete
        runner.send_ctrlk('f') # ^K F
        runner.send_keys("f")

        # Verify autocomplete suggestion is displayed
        runner.assert_text_on_screen("Search for: f[ox]", timeout=1.5)
        
        # 4. Accept autocomplete with Tab
        runner.send_keys('\t')
        runner.assert_text_on_screen("Search for: fox_")
        
        # Execute search (requires two enters now, one for search term, one for options)
        runner.send_keys('\n\n')

        runner.assert_cursor_position(1, 17, timeout=1.5) # Cursor at start of 'fox'
        
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_search_autocomplete()
    print("test_search_autocomplete passed!")
