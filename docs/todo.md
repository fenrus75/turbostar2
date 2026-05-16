
# short term items (fixes needed -- agents can automatically add todo items to this section)

- search via ^K F could use some autocompletion (similar to file dialog),
     but based on past searches as source for autocomplete. this means we
     need a global list of past search strings, populated both from ^KF and the
     dialog box option 
- code cleanup
    - window.cpp lines 54-106 is a chain of if statements that could
    	be a switch()
- src/dialog.cpp uses A_REVERSE
     - needs to get explicit colors instead

- we need to update docs/colorscheme.md based on recent additions to
     main.cpp and the file dialog


# mid term items

- improve syntax highlighting
- ^KF search should get a second question (joe style)
     Joe offers "(I)gnore (R)eplace (B)ackwards Bloc(K) (^K H for help)" as
     second question. "K" is "within selection" "I" is ignore case

# long term items

- add a compile output window somehow
- LLM connection window?



# done items (items move here on completion)

- fix search via ^K-F . When done via key bindings (thus status bar), the operation does not actually search. A consecutive ^L does search and to the found item
- file dialog: File Open case. when you navigate (in the file listing section) to some file and hit Enter, you don't go directly to the editor with the file,
    but instead you go first to the entry box at the top of the dialog. THis is a redundant but annoying-to-the-user step, we should just accept enter instantly
- test suite failures need to be fixed; 
   - likely the search item above will fix at least some failures
- show dirty/clean state of the windows in the window title bar somehow, and in the window list menu
