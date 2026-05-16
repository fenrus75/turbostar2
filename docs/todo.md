
# short term items (fixes needed -- agents can automatically add todo items to this section)

- search via ^K F could use some autocompletion (similar to file dialog),
     but based on past searches as source for autocomplete. this means we
     need a global list of past search strings, populated both from ^KF and the
     dialog box option 

# mid term items

- improve syntax highlighting
- ^KF search should get a second question (joe style)
     Joe offers "(I)gnore (R)eplace (B)ackwards Bloc(K) (^K H for help)" as
     second question. "K" is "within selection" "I" is ignore case

# long term items

- add a compile output window somehow
   - meson first, maybe later cmake and autoconf
- LLM connection window?
   - chat only first
   - needs libcurl


# done items (items move here on completion)

- run a test coverage analysis to see if whole areas are not covered by the
    test suite

- need to sync the key mapping document in docs/ with recent keyboard
  shortcut additions

- we need to update docs/colorscheme.md based on recent additions to
     main.cpp and the file dialog

- on making the backup ~ file we should use a move/rename style operation
  rather than writing out a new copy; if the disk is full the rename will
  succeed but not the new writeout - the new writeout would thus lead
  to data corruption.

- add ^KL for "go to line" - ask the line number in the status bar and then move the Y cursor to that line

- On saving over an existing file we should make a filename~ style backup file

- File->Save acts as File-Save As in that it asks for a filename - only Save As should ask for a filename
    unless no current filename exists

- we should extend the default test timeout to 60 seconds as we do many delays

- add ^K S as a shortcut for save (not save-as, so use existing filename)

- needs_render = true should become a method so that we can add hooks/etc into
    it centrally later

- src/dialog.cpp uses A_REVERSE
     - needs to get explicit colors instead

- we broke cursor navigation. if the cursor is on the left most character of a line, it does not
    go to the end of the previous line on using the cursor-left key

- our testing framework struggles with finding the turbostar binary as we
  can use different buildroot -- can we teach meson to copy the result to
  our testrun directory?

- code cleanup
    - window.cpp lines 54-106 is a chain of if statements that could
    	be a switch()

- fix search via ^K-F . When done via key bindings (thus status bar), the operation does not actually search. A consecutive ^L does search and to the found item
- file dialog: File Open case. when you navigate (in the file listing section) to some file and hit Enter, you don't go directly to the editor with the file,
    but instead you go first to the entry box at the top of the dialog. THis is a redundant but annoying-to-the-user step, we should just accept enter instantly
- test suite failures need to be fixed; 
   - likely the search item above will fix at least some failures
- show dirty/clean state of the windows in the window title bar somehow, and in the window list menu
- test suite running environment definition
   - currently a bit of a mess, we should make a testrun/ directory and make
     that the directory tests ALWAYS run from as CWD.. This impacts data
     directory paths, where to find the turbostat binary etc etc but at
     least it will be a predictable place.
