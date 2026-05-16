# File Dialog specification

A Turbo-Pascal for DOS style file dialog

# Window Title

Since we use the same dialog for File Open and Save operations, we need the
title of the Dialog box window to be an argument to the constructor.
The constructur also needs an argument for "autocomplete", which is
generally set to True for Load operations but not for Save As opertions.

# "Name" element

The "N" of "Name" is the alt-hotkey for the filename field and is colored
yellow, the other letters of the word "Name" are bright white if
the cursor is in the filename entry box, and black if the cursor is not in
the entry box.

# The filename entry box element

This is where the user can type a filename, but also, if the user navigates
through the filesystem view, this field should auto-update with the filename
under the cursor.

The filename entry box has a dark blue background and typed text is of a
bright white color.

If autocomplete is set, and the filename entry box is active, the files in the current directory are used to
provide a suggested completion for the file, in a gray ("white without
bold") color. When multiple completions are possible, the one with the
newest modification date is used.

# The Ok button

The Ok button is on the same horizontal level as the filename entry box
(see `button-recipie.md` for how to make buttons), with the "O" being the
hotkey. 

# one horizontal open line

# The "Files" element
The text "Files" is styled similar to the "Name" element, with the obvious
difference that the bright white highlight is applied if the cursor is in
the filesystem view. Alt-F is the hotkey.

# The Filesystem view

The filesystem view has a Cyan background and consists of two columns,
separated by a blue vertical line. There is a horizonal scrollbar at the
bottom in turbo pascal style (same as the main edit window)

The filesystem view has files and directories listed, initially from the
current directory but navigation by going into directories, or following
".." to go to the parent is possible.

Files are normally black in color.

When the filesystem view is selected, the active file is highlighted with a
bright yellow color.

Cursor navigation (up/down/left/right) works as normal. Note that there is
Only horizontal scrolling if there are more files than there is space in the
dialog box, not vertical scrolling.
The Filename entry box gets updated with the name of the file under the
cursor.

# The Cancel button

The is a "Cancel" button next to the filesystem view, at the same horizontal
level as the top line of the filesystem view

# File information section

The 2 lowest lines of the dialog box are Bright Blue on Dark Blue, and
contain information about the "current file".
The first line will contain the full name of the currently shown directory
The second line will show the filename, file size and file modification
times of the currently active file. When the entry box is active, the active
file is whatever the user is typing in that box, while if the filesystem
view is active, the cursor in that view determines the active file.




