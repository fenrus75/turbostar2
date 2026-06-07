import time
from turbostar_runner import *

def test_lsp_missing_server():
    runner = TurbostarRunner()
    try:
        # 1. Start the editor opening a Python file with LSP enabled
        # pylsp is not installed, so it should handle this gracefully without hanging
        runner.start(filename="test.py", use_lsp=True)
        
        # 2. Type some Python code to ensure the editor is responsive
        runner.send_keys("def hello_world():\n")
        runner.send_keys("    print('hello')\n")
        
        # 3. Save the document
        runner.send_ctrlk('s')
        time.sleep(1.0)
        
        # 4. Verify in the log that it skipped starting pylsp or failed gracefully
        runner.assert_in_log("pylsp")
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_lsp_missing_server()
    print("test_lsp_missing_server passed!")
