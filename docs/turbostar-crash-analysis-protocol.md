# Turbostar crash analysis protocol

When a crash report comes in, we follow the "What - How - Where" analysis protocol
that is described in this document.

Much of the language in the document uses an example of a NULL pointer crash,
but this is for illustration only; the protocol applies to all types of crashes.

## What step

The "What" step is about knowing what happened to cause the crash, for example 
"in this line of code in this function, the pointer F was NULL so dereferencing caused a
crash". To get to a complete what description, you will need to combine
the crash report with the actual source code that is implicated in the crash report.

Summarize the result of the "What" step to the user, and include this as the first 
paragraph in the git commit message for any fixes or enhancements

## How step

The "How" step's objective is to trace through the source code to find out
HOW the condition that caused the crash could have happened.

There are three cases, split into subsections below.

### Positive How

If you can explicitly trace the assignment of the NULL value to the variable
that later gets dereferenced, this is a "Positive How"; an
explicit action directly caused the NULL to appear.

As part of tracing the "How" step, repeat the "How" question at least once (example):

Q1: How did the NULL value get to the crash site
A1: This field became NULL because at line 54 of file.c it got assigned to the return value of function get_foo();

Q2: How did the function get_foo() end up returning NULL?
A2: get_foo() only returns NULL if <some global condition happens>
...

Some analysis ends up needing three or four rounds of the "How" question to
get to a plausible root cause. 

Include each round of How question and answer
in your report to the user and in the commit message.

### Negative How

Sometimes the "NULL" value originates from the original allocation of the
data, and nothing assigned a real value to the field in question.
The right question to chase at that point is "How did this field NOT get a
value assigned?"

This will generally require looking at prevailing code patterns for this
field and find the places where the field is normally assigned a value.
Once a normal pattern that sets the value is found, the key question centers
around why these normal patterns did NOT trigger for the case in question.

Sometimes the answer lies in global conditions or parameters passed
incorrectly, but race conditions should always be considered.

### Unknowable How

The crash information may not have sufficient information to trace down
either case — at which point there is an "Unknowable How" — report to the
user a brief summary of what was analysed and what could not be determined.

In the case of an Unknowable How, the goal is not to make a fix, but
the goal is to improve the code to ensure that if this happens again,
more information is available to get to a conclusion. A typical
way to do this is to add extra logging in strategic places.


## Where step

The last step in the "What-How-Where" protocol is about where the fix needs
to go.
The default option, when all else fails, is to add a check at the site of
the What, for example a NULL pointer check.

As part of finding a good place for a fix, consider how the error can be
handled at this level. The "How" step should guide you in this process.

