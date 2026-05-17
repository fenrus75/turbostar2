import time
import os
from turbostar_runner import TurbostarRunner

def test_format_document():
    runner = TurbostarRunner()
    ref_file = "tests/data/format_golden.txt"
    try:
        runner.start()
        # 1. Type messy C++ code
        runner.send_keys("int main(){\n")
        runner.send_keys("int x=5;\n")
        runner.send_keys("if(x==5){x++;}\n")
        runner.send_keys("return 0;}")
        
        # 2. Trigger Format Document via ^KJ
        runner.send_ctrlk('j')
        time.sleep(1.0) # Give clang-format time
        
        # 3. Verify formatting using primitive
        try:
            runner.assert_content_is(ref_file)
        except Exception as e:
            print(f"Log contents:\n{runner.get_log()}")
            raise e
        
        # 4. Test Undo of format
        runner.send_keys('\x1f') # Ctrl-_ (Undo)
        time.sleep(0.5)
        # Content should be back to messy state
        # (Quick check on screen is fine for undo state)
        screen_undone = "\n".join(runner.screen.display)
        runner.assert_text_on_screen("int x=5;")
        
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_format_document()
    print("test_format passed!")
