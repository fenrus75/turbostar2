from turbostar_runner import TurbostarRunner
import time

def test_doubled_ctrl_sequences():
    runner = TurbostarRunner()
    try:
        runner.start()
        time.sleep(0.5)
        
        runner.send_keys("Test Sequence")
        
        # 1. Test ^K^B (Ctrl-K, then Ctrl-B)
        # ^K is \x0b, ^B is \x02
        runner.send_keys('\x0b\x02') 
        
        # 2. Test ^K^K (Ctrl-K, then Ctrl-K)
        # Second ^K is \x0b
        runner.send_keys('\x0b\x0b')
        
        time.sleep(0.5)
        log = runner.get_log()
        
        # Verify both were handled
        assert "K-block: Set Selection Begin" in log
        assert "K-block: Set Selection End" in log
        
        # 3. Verify ^K^H (Hide Selection)
        runner.send_keys('\x0b\x08') # \x08 is ^H (Backspace usually, but let's see)
        # Actually ^H is 8.
        time.sleep(0.5)
        log = runner.get_log()
        assert "K-block: Clear Selection" in log
        
        runner.send_keys('\x0b' + 'q')
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_doubled_ctrl_sequences()
    print("test_ctrl_sequences passed!")
