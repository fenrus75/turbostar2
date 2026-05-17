import time
from turbostar_runner import TurbostarRunner

def test_lsp_selection():
    runner = TurbostarRunner()
    try:
        runner.start(filename="test.cpp", use_lsp=True)
        time.sleep(1.0)
        
        # 1. Type some code
        runner.send_keys("void my_func() {\n")
        runner.send_keys("    int x = 42;\n")
        runner.send_keys("}")
        
        # 2. Wait for clangd to process
        runner.send_ctrlk('s') # Save
        time.sleep(1.0)
        
        # 3. Move cursor to inside 'x' (line 2, char 8)
        runner.send_keys('\x1b[A') # Up
        runner.send_keys('\x05') # EOL
        runner.send_keys('\x1b[D', count=8) # Left to 'x'
        
        # 4. Expand Selection
        runner.send_ctrlk(']')
        time.sleep(0.5)
        
        # 5. Expand again
        runner.send_ctrlk(']')
        time.sleep(0.5)

        # 6. Delete block
        runner.send_ctrlk('y')
        
        runner.send_ctrlk('q') # Ctrl-C
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_lsp_selection()
    print("test_lsp_selection passed (smoke test)!")
