import os
import glob

docs_dir = "docs"
output_file = os.path.join(docs_dir, "merged_improvements.md")

files = glob.glob(os.path.join(docs_dir, "improvement*.md"))
files.sort()

with open(output_file, "w") as out_f:
    out_f.write("# Merged Improvement Documents\n\n")
    out_f.write("This document comprehensively merges all improvement specs into a single file.\n\n")
    for file_path in files:
        if file_path == output_file:
            continue
        out_f.write(f"## {os.path.basename(file_path)}\n\n")
        with open(file_path, "r") as in_f:
            out_f.write(in_f.read())
        out_f.write("\n\n---\n\n")

print(f"Successfully merged {len(files)} files into {output_file}")
