import re
import sys

with open("ASIO-Ultra.html", "r", encoding="utf-8") as f:
    lines = f.readlines()

# Stage 1: Move "感谢名单" section
# Find boundaries of 感谢名单
start_idx = -1
end_idx = -1
for i, line in enumerate(lines):
    if "<!-- 感谢名单 -->" in line:
        start_idx = i
    elif start_idx != -1 and "<!-- 黑名单 -->" in line:
        end_idx = i
        break

if start_idx != -1 and end_idx != -1:
    thanks_block = lines[start_idx:end_idx]
    
    # Remove from original location
    del lines[start_idx:end_idx]
    
    # Find insertion point (before PART A)
    insert_idx = -1
    for i, line in enumerate(lines):
        if "<!-- ==================== PART A: 驱动逆向审查表 ==================== -->" in line:
            insert_idx = i
            break
            
    if insert_idx != -1:
        # Insert it
        lines = lines[:insert_idx] + ["<br>\n"] + thanks_block + ["\n"] + lines[insert_idx:]

# Stage 2: Convert <div class="brand-sec card"> to <details>
# Using a crude state machine since the HTML is nicely formatted
# Each brand block starts with `<div class="brand-sec card"` and ends with `</div>` before an empty line or comment.
# Actually, the easiest way to find the closing </div> is counting div depths.

new_lines = []
in_brand = False
div_depth = 0

for line in lines:
    if "class=\"brand-sec card\"" in line and not in_brand:
        in_brand = True
        div_depth = 1
        line = line.replace("<div ", "<details ")
        new_lines.append(line)
        continue
        
    if in_brand:
        # Check if line is the h3 tag
        if line.strip().startswith("<h3>"):
            # Wrap h3 in summary
            line = re.sub(r'^(.*?)<h3(.*?)</h3>(.*)$', r'\1<summary style="cursor:pointer; outline:none;"><h3 style="display:inline-block; margin-bottom:0;"\2</h3></summary>\3', line)
            new_lines.append(line)
            continue
            
        div_depth += line.count("<div")
        div_depth -= line.count("</div")
        
        if div_depth == 0:
            # We found the closing tag
            in_brand = False
            line = line.replace("</div>", "</details>")
            new_lines.append(line)
        else:
            new_lines.append(line)
    else:
        new_lines.append(line)

# Add CSS for details summary and transitions
css_block = """
details.brand-sec > summary { list-style: none; user-select: none; }
details.brand-sec > summary::-webkit-details-marker { display: none; }
details.brand-sec > summary h3 { margin: 0; padding-bottom: 0; }
details.brand-sec[open] > summary h3 { margin-bottom: 12px; }
details.brand-sec[open] { animation: fadeIn 0.4s ease-out; }
@keyframes fadeIn { from { opacity: 0.8; transform: translateY(-4px); } to { opacity: 1; transform: translateY(0); } }
"""

# Insert CSS into <style>
style_end = -1
for i, line in enumerate(new_lines):
    if "</style>" in line:
        style_end = i
        break

if style_end != -1:
    new_lines.insert(style_end, css_block)

with open("ASIO-Ultra.html", "w", encoding="utf-8") as f:
    f.writelines(new_lines)

print("SUCCESS: HTML folded and gratitude list moved!")
