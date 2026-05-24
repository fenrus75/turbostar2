from turbostar_runner import *
import time

def test_list_tests():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # Open Agent Window
        runner.send_keys('\x1b' + 'a') # Alt-A
        time.sleep(1.0)
        
        # Type a request to list tests
        runner.send_keys("fs_list_tests()")
        runner.send_keys('\n')
        
        # Wait for agent to process and show the list
        # We expect a markdown table with test names
        runner.assert_text_on_screen("Available Tests", timeout=10.0)
        runner.assert_text_on_screen("unit_event_logger", timeout=2.0)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_list_tests()
    print("test_list_tests passed!")
