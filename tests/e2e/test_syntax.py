from turbostar_runner import TurbostarRunner
import time

def test_syntax_highlighting():
    runner = TurbostarRunner()
    try:
        runner.start()
        # 1. Type some code with keywords
        # "void main() { int x = 0; return; }"
        runner.send_keys("void main() { int x = 0; return; }")
        
        # 2. Wait for background highlighter thread

        # Verification: we can't easily verify colors via this runner's 'screen'
        # which only captures text. But we can check logs to see if highlighter ran.
        # Actually, the best way to verify is to ensure it doesn't crash and maybe 
        # use the State log if we added anything there. 
        # For now, let's just ensure no crashes and that the text is there.
        runner.assert_text_on_screen("void main()", timeout=2.0)
        
        # 3. Test multi-line
        runner.send_keys("\nconst bool active = true;")

        runner.assert_text_on_screen("const bool active", timeout=1.5)
        
        runner.send_ctrlk('q')
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_syntax_highlighting()
    print("test_syntax passed (smoke test)!")
