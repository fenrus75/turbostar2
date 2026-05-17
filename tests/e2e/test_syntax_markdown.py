import time
from turbostar_runner import TurbostarRunner

def test_syntax_markdown():
    runner = TurbostarRunner()
    try:
        runner.start(filename="test.md")
        # 1. Type some Markdown code
        runner.send_keys("# Heading 1\n")
        runner.send_keys("This is a **bold** statement.\n")
        runner.send_keys("- List item 1\n")
        
        # We can't easily assert color pairs via pyte in our current test setup,
        # but we can at least assert the text is rendered and doesn't crash the highlighter thread.
        runner.assert_text_on_screen("# Heading 1")
        runner.assert_text_on_screen("**bold**")
        runner.assert_text_on_screen("- List item")
        
        runner.send_ctrlk('q') # Ctrl-C
        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_syntax_markdown()
    print("test_syntax_markdown passed (smoke test)!")
