import re
import pytest
from turbostar_runner import TurbostarRunner

def test_exit_delay():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # Wait a moment to ensure all background threads (LSP, indexing, etc) have spawned
        runner.assert_in_log("UI initialized.", timeout=5.0)
        # Give it just a tiny bit more time to let threads settle
        import time
        time.sleep(0.5)

        # Trigger quit
        runner.send_ctrlk('q')
        runner.wait(timeout=5)

        log_contents = runner.get_log()
        print(f"LOG:\n{log_contents}")

        # Parse timestamps. Format: [000123ms] Message...
        pattern = re.compile(r"\[(\d{6})ms\] (.*)")
        
        exit_requested_time = None
        final_time = None

        for line in log_contents.splitlines():
            match = pattern.search(line)
            if match:
                ts = int(match.group(1))
                msg = match.group(2)
                
                if "Application exit requested" in msg:
                    if exit_requested_time is None:
                        exit_requested_time = ts
                
                # Keep tracking the very last timestamp logged
                final_time = ts
        
        assert exit_requested_time is not None, "Did not find 'Application exit requested' in log"
        assert final_time is not None, "Did not find any timestamps in log"
        
        delay = final_time - exit_requested_time
        print(f"Exit delay: {delay}ms")
        
        # Assert delay is within 150ms
        assert delay <= 150, f"Exit delay was {delay}ms, which exceeds the 150ms threshold"

    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_exit_delay()
