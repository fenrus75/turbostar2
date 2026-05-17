from turbostar_runner import TurbostarRunner

def test_startup_and_quit():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # Send Ctrl-C to trigger fallback quit since 'q' is no longer hardcoded
        runner.send_keys('\x0b' + 'q')
        
        # Wait for the process to exit
        runner.wait(timeout=2)

        # Read the log file and verify events
        log_contents = runner.get_log()
        print(f"LOG:\n{log_contents}")

        assert "Application started." in log_contents
        assert "UI initialized." in log_contents
        assert "K-block: Quit (Abort)" in log_contents
        assert "Dispatching quit event." in log_contents
        assert "Exiting application loop." in log_contents

    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_startup_and_quit()
