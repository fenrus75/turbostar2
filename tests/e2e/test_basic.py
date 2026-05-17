from turbostar_runner import TurbostarRunner

def test_startup_and_quit():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # Send Ctrl-C to trigger fallback quit since 'q' is no longer hardcoded
        runner.send_ctrlk('q')
        
        # Wait for the process to exit
        runner.wait(timeout=5)

        # Read the log file and verify events
        log_contents = runner.get_log()
        print(f"LOG:\n{log_contents}")

        runner.assert_in_log("Application started.")
        runner.assert_in_log("UI initialized.")
        runner.assert_in_log("K-block: Quit (Abort)")
        runner.assert_in_log("Dispatching quit event.")
        runner.assert_in_log("Exiting application loop.")

    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_startup_and_quit()
