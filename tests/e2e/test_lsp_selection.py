import time
from turbostar_runner import *

def test_lsp_selection():
    runner = TurbostarRunner()
    try:
        runner.start(filename="test.cpp", use_lsp=True)
        # 1. Type some code
        runner.send_keys("void my_func() {\n")
        runner.send_keys("    int x = 42;\n")
        runner.send_keys("}")
        
        # 2. Wait for clangd to process
        runner.send_ctrlk('s') # Save
        time.sleep(1.0)
        
        # 3. Move cursor to inside 'x' (line 2, char 8)
        runner.send_keys(KEY_UP) # Up
        runner.send_keys(KEY_CTRL_E) # EOL
        runner.send_keys(KEY_LEFT, count=8) # Left to 'x'
        
        # 4. Expand Selection
        runner.send_ctrlk(']')
        time.sleep(0.5)
        
        # 5. Expand again
        runner.send_ctrlk(']')
        time.sleep(0.5)

        # 6. Delete block
        runner.send_ctrlk('y')
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_lsp_selection()
    print("test_lsp_selection passed (smoke test)!")
