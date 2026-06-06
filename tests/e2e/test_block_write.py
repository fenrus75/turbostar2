import os
import tempfile
import time
from turbostar_runner import *

def test_block_write():
    runner = TurbostarRunner()
    
    # Create temp files for writing
    with tempfile.NamedTemporaryFile(suffix=".txt", delete=False) as tmp1:
        block_file = tmp1.name
    with tempfile.NamedTemporaryFile(suffix=".txt", delete=False) as tmp2:
        whole_file = tmp2.name

    try:
        os.remove(block_file)
        os.remove(whole_file)
    except OSError:
        pass

    try:
        runner.start()
        
        # 1. Setup multi-line text
        runner.send_keys("Line 1\nLine 2\nLine 3\nLine 4")
        runner.assert_text_on_screen("Line 1")
        runner.assert_text_on_screen("Line 4")

        # 2. Select Line 2 and Line 3
        # Move to line 2, start
        runner.move_cursor_to_line(2)
        runner.send_keys(KEY_CTRL_A)
        runner.send_ctrlk('b') # Selection begin

        # Move to line 3, end
        runner.move_cursor_to_line(3)
        runner.send_keys(KEY_CTRL_E)
        runner.send_ctrlk('k') # Selection end

        # 3. Trigger Block Write (^KW)
        runner.send_ctrlk('w')
        runner.assert_text_on_screen("Write Block to File", timeout=2.0)

        # 4. Type filename for writing the block
        runner.send_keys(KEY_CTRL_Y) # Clear pre-filled
        runner.send_keys(block_file + '\n')
        runner.assert_text_not_on_screen("Write Block to File", timeout=2.0)

        # Verify that ONLY the block (Line 2 and Line 3) was written
        assert os.path.exists(block_file), "Block file was not written"
        with open(block_file, 'r') as f:
            content = f.read()
        assert content == "Line 2\nLine 3", f"Expected 'Line 2\\nLine 3', got {repr(content)}"

        # 4.5. Test that menu option "File -> Save as..." writes the whole file even with active selection
        # (Selection is still active here)
        runner.send_keys(KEY_ESC + 'f')
        runner.assert_text_on_screen("Save as...", timeout=2.0)
        runner.send_keys('a') # Select "Save as..."
        runner.assert_text_on_screen("Save File As", timeout=2.0)
        
        # Cancel the dialog
        runner.send_keys(KEY_ESC)
        runner.assert_text_not_on_screen("Save File As", timeout=2.0)

        # 5. Clear selection (^KH)
        runner.send_ctrlk('h')

        # 6. Trigger Save As (^KW) with NO active selection
        runner.send_ctrlk('w')
        runner.assert_text_on_screen("Save File As", timeout=2.0)

        # 7. Type filename for saving the whole file
        runner.send_keys(KEY_CTRL_Y) # Clear pre-filled
        runner.send_keys(whole_file + '\n')
        runner.assert_text_not_on_screen("Save File As", timeout=2.0)

        # Verify that the entire document was saved
        assert os.path.exists(whole_file), "Whole file was not saved"
        with open(whole_file, 'r') as f:
            content = f.read()
        assert content == "Line 1\nLine 2\nLine 3\nLine 4", f"Expected full content, got {repr(content)}"

    finally:
        runner.cleanup()
        try:
            os.remove(block_file)
            os.remove(whole_file)
        except OSError:
            pass

if __name__ == "__main__":
    test_block_write()
    print("test_block_write passed!")
