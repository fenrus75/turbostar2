from turbostar_runner import TurbostarRunner
import time
import os

def test_assert_content():
    runner = TurbostarRunner()
    ref_file = "tests/data/golden_sample.txt"
    
    try:
        runner.start()
        time.sleep(0.5)
        
        # 1. Type the content of golden_sample.txt
        runner.send_keys("Golden Data:\n")
        runner.send_keys("Selection, Load, Save.\n")
        runner.send_keys("Full UTF-8: €¢£¥\n")
        runner.send_keys("End of data.")
        
        # 2. Assert content matches
        runner.assert_content_is(ref_file)
        
        runner.send_keys('\x0b' + 'q')
        runner.wait(timeout=2)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_assert_content()
    print("test_assert_content passed!")
