#!/usr/bin/env python3
import sys
import subprocess
import shutil

def main():
    if len(sys.argv) < 2:
        print("Usage: verify_html.py <html-file-1> <html-file-2> ...")
        sys.exit(1)

    tidy_path = shutil.which("tidy")
    if not tidy_path:
        print("Warning: tidy not found on system. Skipping verification.")
        sys.exit(77) # 77 is the standard exit code for skipped tests in Meson

    has_errors = False
    for filename in sys.argv[1:]:
        # Run tidy with -errors and -quiet.
        # We redirect stdout to DEVNULL since -errors -quiet writes only warnings/errors to stderr.
        result = subprocess.run(
            [tidy_path, "-errors", "-quiet", filename],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True
        )
        
        # exit status 2 indicates errors. exit status 1 indicates warnings. exit status 0 is success.
        if result.returncode in (1, 2):
            print(f"Error: {filename} has HTML errors or warnings:")
            print(result.stderr)
            has_errors = True
        else:
            print(f"{filename} is correct.")

    if has_errors:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == "__main__":
    main()
