#!/usr/bin/env python3
"""Writes coverflow_files.h: the files Cover Flow carries inside eboot.bin
(its compiled shaders and its home-screen icon) as byte arrays, with the path
each goes to in the title folder. Run it after changing a shader or the icon."""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.abspath(os.path.join(here, "..", ".."))
files = [
    ("shaders/glsl/coverflow/cover.vert.spv", "shaders/glsl/coverflow/cover.vert.spv"),
    ("shaders/glsl/coverflow/cover.frag.spv", "shaders/glsl/coverflow/cover.frag.spv"),
    ("shaders/glsl/coverflow/background.vert.spv", "shaders/glsl/coverflow/background.vert.spv"),
    ("shaders/glsl/coverflow/background.frag.spv", "shaders/glsl/coverflow/background.frag.spv"),
    ("ps5/sce_sys/icon0.png", "sce_sys/icon0.png"),
    # The bold face for headings: an in-app update only replaces eboot.bin, so it travels inside it
    ("examples/coverflow/assets/Roboto-Bold.ttf", "assets/Roboto-Bold.ttf"),
]
out = ["// Made by make_files.py - do not edit", "#pragma once", "",
       "struct EmbeddedFile {", "\tconst char *path;", "\tconst unsigned char *data;", "\tsize_t size;", "};", ""]
names = []
for i, (src, dst) in enumerate(files):
    data = open(os.path.join(root, src), "rb").read()
    name = "embedded_%d" % i
    names.append((name, dst, len(data)))
    out.append("static const unsigned char %s[%d] = {" % (name, len(data)))
    for j in range(0, len(data), 24):
        out.append("\t" + ",".join("0x%02x" % b for b in data[j:j + 24]) + ",")
    out.append("};")
out.append("")
out.append("static const EmbeddedFile embeddedFiles[] = {")
for name, dst, size in names:
    out.append('\t{ "%s", %s, %d },' % (dst, name, size))
out.append("};")
open(os.path.join(here, "coverflow_files.h"), "w").write("\n".join(out) + "\n")
print("coverflow_files.h:", ", ".join("%s (%d bytes)" % (d, s) for _, d, s in names))
