import os
import sys
import tempfile
import time
from turbostar_runner import *

def test_ctrl_k_w_no_selection():
    """Verify that ^KW with no selection does NOT open Save As or write the whole file."""
    with tempfile.TemporaryDirectory() as tmpdir:
        input_file = os.path.join(tmpdir, "input.txt")
        output_file = os.path.join(tmpdir, "out_no_sel.txt")
        with open(input_file, "w") as f:
            f.write("Line 1: Alpha\nLine 2: Beta\nLine 3: Gamma\nLine 4: Delta\n")
            
        runner = TurbostarRunner()
        try:
            runner.start(filename=input_file)
            runner.assert_text_on_screen("Line 1: Alpha", timeout=2.0)
            
            # Press ^KW with no block selected
            runner.send_ctrlk('w')
            time.sleep(0.3)
            
            # It should display "No block selected" and NOT show "Save File As"
            runner.assert_text_on_screen("No block selected", timeout=2.0)
            runner.assert_text_not_on_screen("Save File As")
            
            # Send Escape just in case, then quit
            runner.send_keys(KEY_ESC)
            runner.send_ctrlk('q')
            runner.wait(timeout=5)
        finally:
            runner.cleanup()

def test_ctrl_k_w_mouse_selection():
    """Verify that text selected with mouse drag is written by ^KW, not the whole file."""
    with tempfile.TemporaryDirectory() as tmpdir:
        input_file = os.path.join(tmpdir, "input.txt")
        output_file = os.path.join(tmpdir, "out_mouse.txt")
        with open(input_file, "w") as f:
            f.write("Line 1: Alpha\nLine 2: Beta\nLine 3: Gamma\nLine 4: Delta\n")
            
        runner = TurbostarRunner()
        try:
            runner.start(filename=input_file)
            runner.assert_text_on_screen("Line 1: Alpha", timeout=2.0)
            
            # Drag mouse over Line 2 (display x 0 to 12, row 3)
            # SGR coordinate: x_sgr = x + 2 = 2, y_sgr = y + 1 = 4
            runner.send_raw_keys(b"\x1b[<0;2;4M") # down at (0, 3)
            runner.send_raw_keys(b"\x1b[<32;14;4M") # drag to (12, 3)
            runner.send_raw_keys(b"\x1b[<0;14;4m") # release at (12, 3)
            time.sleep(0.3)
            
            runner.send_ctrlk('w')
            runner.assert_text_on_screen("Write Block to File", timeout=2.0)
            runner.send_keys(output_file + '\n')
            time.sleep(0.5)
            
            assert os.path.exists(output_file), f"Output file {output_file} was not created!"
            with open(output_file, "r") as f:
                content = f.read()
            assert content == "Line 2: Beta", f"Expected only 'Line 2: Beta', but got:\n{repr(content)}"
            
            runner.send_ctrlk('q')
            runner.wait(timeout=5)
        finally:
            runner.cleanup()

def test_ctrl_k_w_kb_only():
    """Verify that setting ^KB and moving cursor (without explicit ^KK) defines block to cursor."""
    with tempfile.TemporaryDirectory() as tmpdir:
        input_file = os.path.join(tmpdir, "input.txt")
        output_file = os.path.join(tmpdir, "out_kb.txt")
        with open(input_file, "w") as f:
            f.write("Line 1: Alpha\nLine 2: Beta\nLine 3: Gamma\nLine 4: Delta\n")
            
        runner = TurbostarRunner()
        try:
            runner.start(filename=input_file)
            runner.assert_text_on_screen("Line 1: Alpha", timeout=2.0)
            
            # Move to Line 2, set ^KB
            runner.send_keys(KEY_DOWN)
            runner.send_ctrlk('b')
            # Move to end of Line 2 without ^KK
            runner.send_keys(KEY_RIGHT, count=12)
            
            runner.send_ctrlk('w')
            runner.assert_text_on_screen("Write Block to File", timeout=2.0)
            runner.send_keys(output_file + '\n')
            time.sleep(0.5)
            
            assert os.path.exists(output_file), f"Output file {output_file} was not created!"
            with open(output_file, "r") as f:
                content = f.read()
            assert content == "Line 2: Beta", f"Expected only 'Line 2: Beta', but got:\n{repr(content)}"
            
            runner.send_ctrlk('q')
            runner.wait(timeout=5)
        finally:
            runner.cleanup()

def test_ctrl_k_w_explicit_selection():
    """Verify that explicit ^KB and ^KK writes only the selected block."""
    with tempfile.TemporaryDirectory() as tmpdir:
        input_file = os.path.join(tmpdir, "input.txt")
        output_file = os.path.join(tmpdir, "out_explicit.txt")
        with open(input_file, "w") as f:
            f.write("Line 1: Alpha\nLine 2: Beta\nLine 3: Gamma\nLine 4: Delta\n")
            
        runner = TurbostarRunner()
        try:
            runner.start(filename=input_file)
            runner.assert_text_on_screen("Line 1: Alpha", timeout=2.0)
            
            # Move to Line 2, set ^KB
            runner.send_keys(KEY_DOWN)
            runner.send_ctrlk('b')
            # Move to end of Line 2, set ^KK
            runner.send_keys(KEY_RIGHT, count=12)
            runner.send_ctrlk('k')
            
            runner.send_ctrlk('w')
            runner.assert_text_on_screen("Write Block to File", timeout=2.0)
            runner.send_keys(output_file + '\n')
            time.sleep(0.5)
            
            assert os.path.exists(output_file), f"Output file {output_file} was not created!"
            with open(output_file, "r") as f:
                content = f.read()
            assert content == "Line 2: Beta", f"Expected only 'Line 2: Beta', but got:\n{repr(content)}"
            
            runner.send_ctrlk('q')
            runner.wait(timeout=5)
        finally:
            runner.cleanup()

if __name__ == "__main__":
    test_ctrl_k_w_explicit_selection()
    print("test_ctrl_k_w_explicit_selection passed!")
    test_ctrl_k_w_no_selection()
    print("test_ctrl_k_w_no_selection passed!")
    test_ctrl_k_w_mouse_selection()
    print("test_ctrl_k_w_mouse_selection passed!")
    test_ctrl_k_w_kb_only()
    print("test_ctrl_k_w_kb_only passed!")
