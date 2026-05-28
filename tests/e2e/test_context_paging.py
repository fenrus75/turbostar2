import os
import json
import pytest
from turbostar_runner import TurbostarRunner

def test_context_paging(tmp_path):
    project_dir = os.path.join(tmp_path, "project")
    os.makedirs(project_dir)
    
    save1_path = os.path.join(tmp_path, "save1.json")
    save2_path = os.path.join(tmp_path, "save2.json")
    
    runner = TurbostarRunner()
    try:
        # Start and open agent window
        runner.start(extra_args=["--no-welcome-screen", "--project-dir", project_dir, "--agent", "hello"])
        runner.assert_in_log("went idle.", timeout=30.0)
        
        # Send a few dummy queries to ensure conversation_.size() >= 3 so paging works
        runner.send_keys("dummy 1\n")
        runner.assert_in_log("went idle.", timeout=30.0, count=2)
        runner.send_keys("dummy 2\n")
        runner.assert_in_log("went idle.", timeout=30.0, count=3)
        
        # Create first milestone via UI
        runner.send_keys("/milestone First\n")
        runner.assert_in_log("Paged out", timeout=10.0)
        
        # Ask "calculate 1 + 1"
        runner.send_keys("calculate 1 + 1\n")
        runner.assert_in_log("went idle.", timeout=30.0, count=4)
        
        # Create second milestone
        runner.send_keys("/milestone Second\n")
        runner.assert_in_log("Paged out", timeout=10.0, count=2)
        
        # Ask weather
        runner.send_keys("what's the weather in san francisco, california\n")
        runner.assert_in_log("went idle.", timeout=30.0, count=5)
        
        # Create third milestone
        runner.send_keys("/milestone Third\n")
        runner.assert_in_log("Paged out", timeout=10.0, count=3)
        
        # Save state 1
        runner.send_keys(f"/save {save1_path}\n")
        runner.assert_in_log("Conversation saved to:", timeout=5.0)
        
        # Quit via menu (Alt-F -> Exit)
        runner.send_keys("\x1bfx")
    finally:
        runner.cleanup(preserve_home=True)
        
    assert os.path.exists(save1_path)
    
    # ---------------------------------------------------------
    # Run 2: Reload session
    # ---------------------------------------------------------
    runner2 = TurbostarRunner()
    try:
        # Same project-dir, open agent window with a dummy message
        runner2.start(extra_args=["--no-welcome-screen", "--project-dir", project_dir, "--agent", "I am back!"], home_dir=runner.temp_home)
        runner2.assert_in_log("restored active state from", timeout=10.0)
        runner2.assert_in_log("went idle.", timeout=60.0)
        
        runner2.send_keys(f"/save {save2_path}\n")
        runner2.assert_in_log("Conversation saved to:", timeout=5.0)
        
        # Quit via menu (Alt-F -> Exit)
        runner2.send_keys("\x1bfx")
    finally:
        runner2.cleanup()
        
    assert os.path.exists(save2_path)
    
    with open(save2_path, 'r') as f:
        root = json.load(f)
        convo = root.get("conversation", [])
        
    # Analyze convo
    milestones = [m for m in convo if m.get("role") == "system" and "[SYSTEM MEMORY: Milestone Reached]" in m.get("content", "")]
    
    assert len(milestones) >= 3, f"Expected at least 3 milestones, found {len(milestones)}"

if __name__ == "__main__":
    import sys
    if os.environ.get("RUN_SLOW_E2E") != "1":
        print("Skipping slow test. Set RUN_SLOW_E2E=1 to run.")
        sys.exit(77)

    import tempfile
    with tempfile.TemporaryDirectory() as tmpdirname:
        test_context_paging(tmpdirname)