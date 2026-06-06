import time
from turbostar_runner import *

def test_modal_dialogs():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # 1. Type initial text in the document
        runner.send_keys("Initial Document Text")
        runner.assert_text_on_screen("Initial Document Text")
        
        # 2. Open Find Dialog (Alt+S -> F)
        runner.send_keys(KEY_ESC + 's')
        runner.assert_menu_active(timeout=2.0)
        runner.send_keys('f')
        runner.assert_text_on_screen("Find", timeout=2.0)
        
        # 3. Test Mouse Interception: click outside the dialog (e.g. row 10, col 5)
        # SGR coordinate click = 1-based: x=6, y=11
        runner.send_raw_keys(b"\x1b[<0;6;11M")
        time.sleep(0.05)
        runner.send_raw_keys(b"\x1b[<0;6;11m")
        time.sleep(0.1)
        
        # 4. Type "Z" - it should go into the dialog textbox, not the document
        runner.send_keys("Z")
        time.sleep(0.1)
        
        # 5. Cancel dialog (ESC)
        runner.send_keys(KEY_ESC)
        time.sleep(0.5)
        
        # Verify dialog is closed and document did NOT receive "Z" (except maybe if it was in the dialog, but not the document)
        runner.assert_text_not_on_screen("Find")
        # The document text should still be exactly "Initial Document Text" (no "Z" added to it)
        runner.assert_text_not_on_screen("Initial Document TextZ")
        runner.assert_text_on_screen("Initial Document Text")
        
        # 6. Test Keyboard Shortcut Interception: open Find dialog again
        runner.send_keys(KEY_ESC + 's')
        runner.send_keys('f')
        runner.assert_text_on_screen("Find", timeout=2.0)
        
        # Press F2 (Save shortcut). Since the dialog is active, it must not trigger the Save As dialog
        runner.send_keys('\x1bOQ')
        time.sleep(0.5)
        
        # If F2 was not blocked, "Save File As" dialog would be open. Check that it is NOT open!
        runner.assert_text_not_on_screen("Save File As")
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_modal_dialogs()
    print("test_modal_dialogs passed!")
