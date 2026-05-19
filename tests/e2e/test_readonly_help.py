from turbostar_runner import *

def test_readonly_help():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # Open help window (Alt+H -> Help)
        # Using the standard Alt-h mapping as in other E2E tests
        runner.send_keys(f'{KEY_ESC}h')
        
        # Wait for the Help menu to activate, then press Enter
        runner.assert_in_log("Menu activated: Help")
        runner.send_keys('\n')

        # Ensure the Help window opens
        runner.assert_text_on_screen("Help")
        runner.assert_text_not_on_screen("Help*")

        # Type some text to attempt modification
        runner.send_keys("SHOULD NOT APPEAR")

        # Wait a moment to ensure no background processing causes it to appear later
        # Since we shouldn't use sleep without a clear poll, we just check that the text
        # didn't appear immediately.
        runner.assert_text_not_on_screen("SHOULD NOT APPEAR", timeout=1.0)
        
        # The window title should still not have a '*'
        runner.assert_text_not_on_screen("Help*")

        # Close help and exit
        runner.send_ctrlk('x')
    except Exception as e:
        print(f"FAILED. Log:\n{runner.get_log()}")
        raise e
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_readonly_help()
