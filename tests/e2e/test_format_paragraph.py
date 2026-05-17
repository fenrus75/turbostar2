import time
import os
from turbostar_runner import TurbostarRunner

def test_format_paragraph():
    runner = TurbostarRunner()
    step1_gold = "tests/data/format_para_step1.txt"
    step2_gold = "tests/data/format_para_step2.txt"
    try:
        runner.start()
        time.sleep(0.5)
        
        # 1. Type two paragraphs of messy C++ code
        runner.send_keys("void p1(){int a=1;}\n")
        runner.send_keys("\n") # Blank line
        runner.send_keys("void p2(){int b=2;}\n")
        
        # 2. Move cursor back to Para 1
        runner.send_keys('\x0b' + 'u') # ^K U (Top)
        time.sleep(0.5)
        
        # 3. Trigger Format Paragraph via ^KJ
        runner.send_keys('\x0b' + 'j')
        time.sleep(1.0) # Give clang-format time
        
        # 4. Verify Para 1 is formatted, Para 2 is NOT
        runner.assert_content_is(step1_gold)
        
        # 5. Move to Para 2 and format it
        # Para 1 became 4 lines + 1 blank line = 5 lines.
        # Cursor was at 1:1, move down 5 times to 6:1 (Para 2)
        runner.send_keys('\x1b[B', count=5) 
        runner.send_keys('\x0b' + 'j')
        time.sleep(1.0)
        
        # 6. Verify both are formatted
        runner.assert_content_is(step2_gold)

        runner.send_keys('\x0b' + 'q') # Ctrl-C
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_format_paragraph()
    print("test_format_paragraph passed!")
