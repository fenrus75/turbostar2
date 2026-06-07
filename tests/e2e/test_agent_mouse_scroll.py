import time
import os
from turbostar_runner import *

def test_agent_mouse_scroll():
    runner = TurbostarRunner()
    
    # Pre-generate paths for /save with unique, non-overlapping names
    paths = [f"/tmp/ts_m{c}" for c in "abcdefghij"]
    
    try:
        runner.start()
        
        # 1. Open Agent Window via Alt+A -> O
        runner.send_keys(KEY_ESC + 'a')
        runner.assert_menu_active(timeout=2.0)
        runner.send_keys('o')
        
        # Wait for agent window to appear
        runner.assert_text_on_screen("Agent Chat", timeout=2.0)
        time.sleep(1.0)
        
        # 2. Type several /save commands to populate the history
        for path in paths:
            # Ensure input box is clear
            runner.send_keys(KEY_CTRL_A)
            runner.send_keys(KEY_BACKSPACE * 50)
            
            cmd = f"/save {path}\n"
            runner.send_keys(cmd)
            time.sleep(0.15)
            
        # Verify the last marker is visible
        runner.assert_text_on_screen("ts_mj", timeout=2.0)
        
        # Verify the first marker is scrolled off-screen
        runner.assert_text_not_on_screen("ts_ma", timeout=2.0)
        
        # 3. Send mouse scroll up events incrementally until ts_ma becomes visible
        # SGR mouse wheel up button = 64
        # Coordinate x=20, y=5 translates to: 1-based x=21, y=6
        found_ma = False
        for _ in range(20):
            runner.send_raw_keys(b"\x1b[<64;21;6M")
            time.sleep(0.1)
            # Let the UI update and check
            runner.assert_text_on_screen("Agent Chat", timeout=0.5)
            if any("ts_ma" in line for line in runner.screen.display):
                found_ma = True
                break
                
        if not found_ma:
            raise AssertionError("ts_ma did not become visible after scrolling up")
            
        # Verify that the last marker is now scrolled out of view
        runner.assert_text_not_on_screen("ts_mj", timeout=2.0)
        
        # 4. Scroll down again incrementally until ts_mj becomes visible
        # SGR mouse wheel down button = 65
        found_mj = False
        for _ in range(20):
            runner.send_raw_keys(b"\x1b[<65;21;6M")
            time.sleep(0.1)
            runner.assert_text_on_screen("Agent Chat", timeout=0.5)
            if any("ts_mj" in line for line in runner.screen.display):
                found_mj = True
                break
                
        if not found_mj:
            raise AssertionError("ts_mj did not become visible after scrolling down")
            
        # Verify that the first marker is back out of view
        runner.assert_text_not_on_screen("ts_ma", timeout=2.0)
        
    except Exception as e:
        if hasattr(runner, 'log_path') and os.path.exists(runner.log_path):
            with open(runner.log_path, 'r') as f:
                print("--- FULL EDITOR LOG ---")
                print(f.read())
        print(f"FAILED. Screen:\n{chr(10).join(runner.screen.display)}")
        raise e
    finally:
        runner.cleanup()
        # Clean up files created under /tmp
        for path in paths:
            if os.path.exists(path):
                try:
                    os.remove(path)
                except:
                    pass

if __name__ == "__main__":
    test_agent_mouse_scroll()
    print("test_agent_mouse_scroll passed!")
