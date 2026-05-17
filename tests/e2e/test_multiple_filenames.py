import time
from turbostar_runner import TurbostarRunner

def test_multiple_filenames():
    runner = TurbostarRunner()
    try:
        # Start with two specific filenames
        runner.start(filename="file1.txt file2.txt")
        # Verify both windows exist in the Window menu
        # Alt+W to open Window menu
        runner.send_keys('\x1b' + 'w')
        time.sleep(0.5)
        
        # Verify filenames are in the menu
        runner.assert_text_on_screen("file1.txt")
        runner.assert_text_on_screen("file2.txt")
        
        # Close menu
        runner.send_keys('\x1b')
        time.sleep(0.2)
        
        # Verify we are in the second file (last opened)
        runner.assert_text_on_screen("file2.txt")
        
        # Switch to first file using Alt+1
        runner.send_keys('\x1b' + '1')
        time.sleep(0.5)
        runner.assert_text_on_screen("file1.txt")

        runner.send_ctrlk('q') # Ctrl-C
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_multiple_filenames()
    print("test_multiple_filenames passed!")
