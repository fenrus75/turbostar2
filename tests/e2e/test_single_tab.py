from turbostar_runner import TurbostarRunner
import time

def test_single_tab():
    runner = TurbostarRunner()
    try:
        runner.start()
        time.sleep(0.5)
        
        # 1. Type a single tab
        runner.send_keys('\t')
        time.sleep(0.5)
        
        # 2. Verify cursor position
        # A tab at col 0 should move to col 8.
        # Logical char pos 1 (after tab) should be display col 8 (0-based) or 9 (1-based).
        try:
            runner.assert_cursor_position(1, 9)
        except Exception as e:
            print(f"FAILED. Log:\n{runner.get_log()}")
            raise e
        
        runner.send_ctrlk('q')
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_single_tab()
    print("test_single_tab passed!")
