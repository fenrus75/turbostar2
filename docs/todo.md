# short term items (fixes needed -- agents can automatically add todo items to this section)

- LSP: similar to how we highlight the matching {}[](), we could highlight the definition of a variable/etc if the
  cursor is on a use of the variable.

  

# mid term items

- support for LSP servers
   - will need to be some per document thread? or will this be a global thread? need arch analysis
   - avoids needing to use tree-sitter, which is a mess in reality
   - seems https://github.com/leon-bckl/lsp-framework is the better C++ framework
       - decision needed: do we want to make this a git submodule? Or do we build our own instead?
       - other options seem to pull in boost and other nightmares
   - we'd use clangd exclusively, which is fine in practice
   - first feature would be "hover" information  that we can put live in the status bar

   - this should get us on the road to better syntax highlighting, but we still need the general syntax highlighting improvements first

# long term items
   

- add a compile output window somehow
   - meson first, maybe later cmake and autoconf
- LLM connection window?
   - chat only first
   - needs libcurl


# done items (items move here on completion)

- improve syntax highlighting
   - multiple languages support (first one: markdown)
   - we will need an abstraction between the syntax highlighting thread and the language, one class per language most likely
   - each class should have a method for "is this filename for me" that returns a bool - the first one to say "yes" wins 
   - need to reevaluate this on "Save As" as the filename changes 
   - need to standardize between languages what the attributes mean, some sort of C++ enum equivalent
   - need to build it so that we can, over time, get to the LSP server approach

- better git integration: key decision: libgit(2) or exec to git? instinct is to use libgit/libgit2 if we can
   - showing git dirty status (clean, dirty, not-in-git) in window somehow as first usage of git integration
     (implemented using background thread and /usr/bin/git)

- allow multiple filenames on the command line and just open them all as separate documents/windows

- Add a `^K` command to select the current `{}` scope (using the new bracket matching logic)
    - `^K[` and `^K{` implemented

- `^G` Matching bracket navigation and visual highlighting

- CI fails because "testrun/" does not exist - the custom meson blurb that copies our binary there should just mkdir -p that directory always

- `^KJ` Paragraph format (implemented using clang-format on the current block of text)

- a settings dialog box (and data backend that uses the ~/.turbostar file)
  the first setting would be prefered coding style (which maps to the clang-format predefined types, and has a "~/.clang-format file" as additional option. If a .clang-format file exists in the project that should always win

- a way to call clang-format on the current file
   - needs to save, run the command, then reload, as one nice operation

- `^KR` Insert file

- implement an undo/redo mechanism
    - `^_` Undo
    - `^^` Redo

- search via ^K F could use some autocompletion (similar to file dialog),
     but based on past searches as source for autocomplete. this means we
     need a global list of past search strings, populated both from ^KF and the
     dialog box option 

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