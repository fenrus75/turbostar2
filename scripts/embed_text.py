#!/usr/bin/env python3
"""Embed a text file as a C++ unsigned char array in a header file.

Usage: embed_text.py <input_file> <output_header> <variable_name>
"""

import sys


def embed_text(input_file: str, output_file: str, var_name: str) -> None:
    with open(input_file, 'rb') as f:
        data = f.read()

    with open(output_file, 'w') as f:
        f.write(f'// Auto-generated from {input_file} — do not edit\n')
        f.write('#pragma once\n\n')
        f.write(f'static const unsigned char {var_name}[] = {{\n')
        for i, byte in enumerate(data):
            f.write(f'0x{byte:02x},')
            if (i + 1) % 16 == 0:
                f.write('\n')
        f.write('0x00\n};\n')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        print(f'Usage: {sys.argv[0]} <input_file> <output_header> <variable_name>',
              file=sys.stderr)
        sys.exit(1)
    embed_text(sys.argv[1], sys.argv[2], sys.argv[3])
