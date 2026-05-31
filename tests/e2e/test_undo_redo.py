import time
from turbostar_runner import *

def test_undo_redo():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Type some text with cursor movement to reset coalescing before the last character
        runner.send_keys("Hello Worl")
        runner.send_keys(KEY_LEFT)
        runner.send_keys(KEY_RIGHT)
        runner.send_keys("d")
        runner.assert_text_on_screen("Hello World")

        # 2. Undo the text insertion (only 'd' should be undone because of cursor movement)
        runner.send_keys(KEY_CTRL_UNDERSCORE) # Ctrl-_ (Undo)

        runner.assert_text_on_screen("Hello Worl", timeout=1.5)

        # 3. Redo the text insertion
        runner.send_keys('\x1e') # Ctrl-^ (Redo)

        runner.assert_text_on_screen("Hello World", timeout=1.5)

        # 4. Test deleting a block (which IS grouped)
        runner.send_ctrlk('b') # ^K B
        runner.send_keys(KEY_LEFT, count=5) # Left 5 times (before 'World')
        runner.send_ctrlk('k') # ^K K
        runner.send_ctrlk('y') # ^K Y (Delete block)
        time.sleep(0.5)
        try:
            runner.assert_text_on_screen("Hello ")
        except AssertionError as e:
            print(f"Log contents:\n{runner.get_log()}")
            raise e
        
        # 5. Undo block delete
        runner.send_keys(KEY_CTRL_UNDERSCORE) # Ctrl-_

        runner.assert_text_on_screen("Hello World", timeout=1.5)

        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_undo_redo()
    print("test_undo_redo passed!")
