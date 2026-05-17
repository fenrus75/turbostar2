import time
from turbostar_runner import TurbostarRunner

def test_window_popup_menu():
    runner = TurbostarRunner()
    try:
        runner.start()
        
        # 1. Spawn popup via Alt-= (Esc + =)
        runner.send_keys('\x1b=')
        
        # The popup menu draws a border ┌, so we can use assert_menu_active since it looks for ┌ on line index 1 (or we can assert ≡ was clicked)
        # Wait, the popup menu is drawn at the button's Y coordinate.
        # Window starts at y=1. Button is at y=1. Popup spawns at y=2.
        # So the top border ┌ is at y=2.
        # assert_menu_active checks line 1. That won't work for the popup menu!
        # Let's write a custom assert or use assert_text_on_screen
        
        # Let's assert that the popup items are visible
        runner.assert_text_on_screen("Git Add", timeout=2.0)
        runner.assert_text_on_screen("Compile File")
        
        # 2. Select "Save" via keyboard. 'Save' is index 0.
        runner.send_keys('\n') # Select first item (Save)
        
        # Should trigger Save As dialog because it's untitled
        runner.assert_text_on_screen("Save File As", timeout=2.0)
        
        # Cancel Save As (using Esc to cancel dialog)
        runner.send_keys('\x1b')
        runner.assert_text_not_on_screen("Save File As", timeout=2.0)
        runner.cleanup()

        # 3. Test Mouse Activation
        runner = TurbostarRunner()
        runner.start()

        # Click the [≡] button. 
        # Window is at x=0, button is at x=6, so 0-based x is 6.
        # Window y=1, button y=1. 0-based y=1.
        # Actually it draws at x_+6, so x=6.
        runner.send_mouse_click(70, 1)
        
        runner.assert_text_on_screen("Compile File", timeout=2.0)
        
        # Click "Close" inside the popup.
        # Popup spawns at y = w->get_y() + 1 = 2
        # Items are drawn at row_y = y_ + 1 + i
        # Items: 0: Save, 1: Git Add, 2: Compile File, 3: Separator, 4: Close
        # Close is at i=4. row_y = 2 + 1 + 4 = 7. (0-based y=7)
        # Popup x = 6. Inside x is 6 to 6+width.
        runner.send_mouse_click(8, 7)
        
        # Window should close and trigger quit or new untitled?
        # If last window closes, it might spawn new untitled or quit. 
        # But let's just assert "Compile File" disappears
        runner.assert_text_not_on_screen("Compile File", timeout=2.0)
        

    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_window_popup_menu()
    print("test_window_popup_menu passed!")
