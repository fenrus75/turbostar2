from turbostar_runner import *
import time
import os

def test_hex_editor_basic():
    runner = TurbostarRunner()
    project_root = os.environ.get('PROJECT_ROOT', os.getcwd())
    testrun_dir = os.path.join(project_root, 'testrun')
    os.makedirs(testrun_dir, exist_ok=True)
    
    test_bin_file = os.path.join(testrun_dir, "test_binary.bin")
    
    # 1. Create a binary file with 11 bytes: "Hello\x00\x01\x02 !\n"
    bin_data = bytes([0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x00, 0x01, 0x02, 0x20, 0x21, 0x0a])
    with open(test_bin_file, 'wb') as f:
        f.write(bin_data)
        
    try:
        # 2. Start editor opening the binary file
        runner.start(filename=test_bin_file)
        
        # 3. Assert offset column and some hex tuples are shown
        # Hello in hex is 48 65 6C 6C 6F (uppercase)
        runner.assert_text_on_screen("00000000", timeout=2.0)
        runner.assert_text_on_screen("48 65 6C 6C 6F", timeout=2.0)
        
        # 4. Check status bar offset display (hex and decimal) and value/ASCII representation
        runner.assert_text_on_screen("Offset: 0x00000000 (0)", timeout=2.0)
        runner.assert_text_on_screen("Value: 0x48 (72, 'H')", timeout=2.0)
        
        # 5. Navigate right (two nibble moves for 1 byte in Hex column) and check offset increments
        runner.send_keys(KEY_RIGHT)
        runner.send_keys(KEY_RIGHT)
        runner.assert_text_on_screen("Offset: 0x00000001 (1)", timeout=2.0)
        runner.assert_text_on_screen("Value: 0x65 (101, 'e')", timeout=2.0)
        
        # 6. Toggle focus to ASCII column
        runner.send_keys('\t')
        
        # Status bar should still reflect current offset and value
        runner.assert_text_on_screen("Offset: 0x00000001 (1)", timeout=2.0)
        runner.assert_text_on_screen("Value: 0x65 (101, 'e')", timeout=2.0)
        
        # 7. Type 'X' in ASCII focus (index 1 which was 'e' (0x65), now becomes 'X' (0x58))
        runner.send_keys('X')
        
        # Cursor advances to index 2
        runner.assert_text_on_screen("Offset: 0x00000002 (2)", timeout=2.0)
        runner.assert_text_on_screen("Value: 0x6C (108, 'l')", timeout=2.0)
        
        # 8. Type at EOF to auto-grow
        # Let's go to EOF (index 11) using ASCII column for faster 1-key-per-byte navigation
        for _ in range(9):
            runner.send_keys(KEY_RIGHT)
        runner.assert_text_on_screen("Offset: 0x0000000B (11)", timeout=2.0)
        runner.assert_text_on_screen("Value: --", timeout=2.0)
        
        # Toggle back to Hex column to type hex digits
        runner.send_keys('\t')
        
        # Type hex digits 'F' 'F' (which appends 0xFF at offset 11)
        runner.send_keys('F')
        runner.send_keys('F')
        
        # Cursor advances to offset 12
        runner.assert_text_on_screen("Offset: 0x0000000C (12)", timeout=2.0)
        runner.assert_text_on_screen("Value: --", timeout=2.0)
        
        # 9. Save file (Ctrl-K S)
        runner.send_ctrlk('s')
        time.sleep(0.5)
        
        # 10. Exit application (Ctrl-K X or Q, let's use Ctrl-K Q)
        runner.send_ctrlk('q')
        runner.wait(timeout=5)
        
        # 11. Read file from disk and assert changes
        # original: [0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x00, 0x01, 0x02, 0x20, 0x21, 0x0a]
        # index 1 modified to 'X' (0x58).
        # appended 0xFF at index 11.
        # expected: [0x48, 0x58, 0x6c, 0x6c, 0x6f, 0x00, 0x01, 0x02, 0x20, 0x21, 0x0a, 0xff]
        with open(test_bin_file, 'rb') as f:
            saved_data = f.read()
            
        expected_data = bytes([0x48, 0x58, 0x6c, 0x6c, 0x6f, 0x00, 0x01, 0x02, 0x20, 0x21, 0x0a, 0xff])
        assert saved_data == expected_data, f"Data mismatch! Expected {expected_data.hex()}, got {saved_data.hex()}"
    except Exception as e:
        print(f"LOG CONTENTS:\n{runner.get_log()}")
        raise e
    finally:
        runner.cleanup()
        if os.path.exists(test_bin_file):
            os.remove(test_bin_file)
        # Clean up backup file if exists
        backup_file = test_bin_file + "~"
        if os.path.exists(backup_file):
            os.remove(backup_file)

if __name__ == "__main__":
    test_hex_editor_basic()
    print("test_hex_editor_basic passed!")
