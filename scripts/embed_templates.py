#!/usr/bin/env python3
"""Embed project template files as static byte arrays in a C++ header file.

Usage: embed_templates.py <templates_dir> <output_header>
"""

import os
import sys


def embed_templates(templates_dir: str, output_file: str) -> None:
    template_files = []

    for root, dirs, files in os.walk(templates_dir):
        dirs.sort()
        files.sort()
        for f in files:
            full_path = os.path.join(root, f)
            rel_path = os.path.relpath(full_path, templates_dir)
            template_files.append((rel_path, full_path))

    template_files.sort(key=lambda x: x[0])

    with open(output_file, 'w') as out:
        out.write('// Auto-generated from templates/ directory — do not edit\n')
        out.write('#pragma once\n')
        out.write('#include <string_view>\n')
        out.write('#include <cstddef>\n\n')
        out.write('struct embedded_template_file {\n')
        out.write('    std::string_view relative_path;\n')
        out.write('    const char *content;\n')
        out.write('    size_t size;\n')
        out.write('};\n\n')

        var_names = []
        for idx, (rel_path, full_path) in enumerate(template_files):
            with open(full_path, 'rb') as f:
                data = f.read()
            var_name = f'template_file_data_{idx}'
            var_names.append((rel_path, var_name, len(data)))

            out.write(f'static const char {var_name}[] = {{\n')
            for i, byte in enumerate(data):
                out.write(f'0x{byte:02x},')
                if (i + 1) % 16 == 0:
                    out.write('\n')
            out.write('0x00\n};\n\n')

        out.write('static const embedded_template_file EMBEDDED_TEMPLATES[] = {\n')
        for rel_path, var_name, size in var_names:
            out.write(f'    {{ "{rel_path}", {var_name}, {size} }},\n')
        out.write('};\n\n')
        out.write(f'static const size_t EMBEDDED_TEMPLATES_COUNT = {len(var_names)};\n')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f'Usage: {sys.argv[0]} <templates_dir> <output_header>', file=sys.stderr)
        sys.exit(1)
    embed_templates(sys.argv[1], sys.argv[2])
