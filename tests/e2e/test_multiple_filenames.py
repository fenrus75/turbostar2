import time
from turbostar_runner import *

def test_multiple_filenames():
    runner = TurbostarRunner()
    try:
        # Start with two specific filenames
        runner.start(filename="file1.txt file2.txt")
        # Verify both windows exist in the Window menu
        # Alt+W to open Window menu
        runner.send_keys(KEY_ESC + 'w')

        # Verify filenames are in the menu
        runner.assert_text_on_screen("file1.txt", timeout=1.5)
        runner.assert_text_on_screen("file2.txt")
        
        # Close menu
        runner.send_keys(KEY_ESC)

        # Verify we are in the second file (last opened)
        runner.assert_text_on_screen("file2.txt", timeout=1.2)
        
        # Switch to first file using Alt+1
        runner.send_keys(KEY_ESC + '1')

        runner.assert_text_on_screen("file1.txt", timeout=1.5)

        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_multiple_filenames()
    print("test_multiple_filenames passed!")
