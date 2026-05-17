import time
from turbostar_runner import TurbostarRunner

def test_undo_redo():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Type some text
        runner.send_keys("Hello World")
        runner.assert_text_on_screen("Hello World")

        # 2. Undo the text insertion (character by character for now if not grouped, 
        # but our implementation currently records each char insertion individually unless grouped.
        # Wait, insert_char is NOT grouped. So each char is an undo step!
        # Let's test undoing the last char 'd'.
        runner.send_keys('\x1f') # Ctrl-_ (Undo)
        time.sleep(0.5)
        runner.assert_text_on_screen("Hello Worl")

        # 3. Redo the text insertion
        runner.send_keys('\x1e') # Ctrl-^ (Redo)
        time.sleep(0.5)
        runner.assert_text_on_screen("Hello World")

        # 4. Test deleting a block (which IS grouped)
        runner.send_ctrlk('b') # ^K B
        runner.send_keys('\x1b[D', count=5) # Left 5 times (before 'World')
        runner.send_ctrlk('k') # ^K K
        runner.send_ctrlk('y') # ^K Y (Delete block)
        time.sleep(0.5)
        try:
            runner.assert_text_on_screen("Hello ")
        except AssertionError as e:
            print(f"Log contents:\n{runner.get_log()}")
            raise e
        
        # 5. Undo block delete
        runner.send_keys('\x1f') # Ctrl-_
        time.sleep(0.5)
        runner.assert_text_on_screen("Hello World")

        runner.send_ctrlk('q') # Ctrl-C
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_undo_redo()
    print("test_undo_redo passed!")
