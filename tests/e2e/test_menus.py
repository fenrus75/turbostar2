import time
from turbostar_runner import TurbostarRunner

def check_menu(key, expected_menu_name):
    runner = TurbostarRunner()
    log_contents = ""
    try:
        runner.start()
        runner.send_keys(f'\x1b{key}') # ESC + key = Alt+key
        runner.send_keys('\x1b')       # ESC to close menu
        time.sleep(0.5)
        runner.send_ctrlk('q')       # Ctrl-C to quit
        try:
            runner.wait(timeout=5)
        except Exception as e:
            print(f"FAILED. Log: {runner.get_log()}")
            raise e
        log_contents = runner.get_log()
        assert f"Menu activated: {expected_menu_name}" in log_contents
    except Exception as e:
        print(f"FAILED. Log: {runner.get_log()}")
        print(f"Screen:\n{chr(10).join(runner.screen.display)}")
        raise e
    finally:
        runner.cleanup()

def test_menu_file(): check_menu('f', 'File')
def test_menu_edit(): check_menu('e', 'Edit')
def test_menu_search(): check_menu('s', 'Search')
def test_menu_help(): check_menu('h', 'Help')

def test_file_exit():
    runner = TurbostarRunner()
    try:
        runner.start()
        runner.send_keys('\x1bf') # Alt-F
        runner.assert_text_on_screen('Exit')
        runner.send_keys('x')    # 'x' selects the "Exit" item
        try:
            runner.wait(timeout=5)
        except Exception as e:
            print(f"FAILED. Log: {runner.get_log()}")
            raise e
        log_contents = runner.get_log()
        assert "Menu activated: File" in log_contents
        assert "Dispatching quit event." in log_contents
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_menu_file()
    test_menu_edit()
    test_menu_search()
    test_menu_help()
    test_file_exit()
