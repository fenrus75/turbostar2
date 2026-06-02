from turbostar_runner import *
import time

def test_mouse_copy():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # 1. Type some text: "Hello World!"
        runner.send_keys("Hello World!")
        runner.assert_text_on_screen("Hello World!")
        
        # 2. Click at 'W' (display x=7, y=2)
        # SGR coordinate is 1-based: x_sgr = 8, y_sgr = 3
        runner.send_raw_keys(b"\x1b[<0;8;3M")
        time.sleep(0.05)
        
        # 3. Drag to after '!' (display x=13, y=2)
        # SGR coordinate: x_sgr = 14, y_sgr = 3
        runner.send_raw_keys(b"\x1b[<32;14;3M")
        time.sleep(0.05)
        
        # 4. Release mouse
        runner.send_raw_keys(b"\x1b[<0;14;3m")
        time.sleep(0.1)
        
        # 5. Quit editor cleanly, discarding changes
        runner.send_ctrlk('q')
        runner.assert_text_on_screen("Save changes to", timeout=2.0)
        runner.send_keys('\x1b' + 'd') # Discard changes
        runner.wait(timeout=5)
        
        # 6. Verify that the clipboard OSC 52 sequence was output
        # Base64 for "World!" is "V29ybGQh". The sequence is ESC ] 52 ; c ; <base64> BEL
        # ESC: \x1b, BEL: \x07
        expected_seq = b"\x1b]52;c;V29ybGQh\x07"
        if expected_seq not in runner.captured_bytes:
            print(f"Captured bytes count: {len(runner.captured_bytes)}")
            print(f"Last 200 captured bytes: {runner.captured_bytes[-200:]}")
            if os.path.exists(runner.log_path):
                with open(runner.log_path, 'r') as f:
                    print("--- EDITOR LOG ---")
                    print(f.read())
            raise AssertionError(f"Expected clipboard sequence {expected_seq} not found in output.")
            
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_mouse_copy()
    print("test_mouse_copy passed!")
