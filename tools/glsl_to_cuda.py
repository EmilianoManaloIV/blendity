"""Task 0170 (ADR 0054): the GPU path tracer's GLSL kernel (src/render/gpu_kernel.h with gpu_glsl.h's procedurals) as
CUDA for the OptiX device, so the two devices run one integrator - the same samples, the same picture. The kernel is
written in a GLSL subset this translation knows (the checks below fail loudly on anything else):
  - the buffers' `layout(...)` lines become pointers in the launch parameters (macros named as the buffers);
  - `out` / `inout` parameters become references;
  - the `.xyz` swizzle write becomes set_xyz(); reads go through glsl.cuh's member functions (`.xyz()` / `.xy()`);
  - float literals get their `f`; `void main()` becomes `run()`; struct constructors become braces;
  - everything goes inside `struct GlslKernel : GlslBase` - the file-scope variables become per-thread members and
    glsl.cuh's built-ins (static members of GlslBase) hide CUDA's of the same names;
  - the hardware / software intersect() variants give way to the OptiX one (optix_intersect in optix_kernel.cu).
Run by tools/optix_ptx_embed.bat; writes build/optix/glsl_kernel.cuh.  Usage: glsl_to_cuda.py <blendity dir> <out>"""
import os
import re
import sys

ROOT, OUT = sys.argv[1], sys.argv[2]


def raw_parts(path):
    """The R"GLSL(...)GLSL" pieces of a header, in order, with kGlslProcedurals' name where the kernel lists it."""
    text = open(path, encoding="utf-8").read()
    parts = []
    for m in re.finditer(r'R"GLSL\((.*?)\)GLSL"|\bkGlslProcedurals\b(?=,)', text, re.S):
        parts.append(m.group(1) if m.group(1) is not None else "@PROCEDURALS@")
    return parts


procedurals = raw_parts(os.path.join(ROOT, "src", "render", "gpu_glsl.h"))
assert len(procedurals) == 1, "gpu_glsl.h: one GLSL piece expected"
kernel = raw_parts(os.path.join(ROOT, "src", "render", "gpu_kernel.h"))
src = "".join(procedurals[0] if p == "@PROCEDURALS@" else p for p in kernel)

# --- the lines GLSL needs and CUDA doesn't
drop = [r"^#version .*$", r"^#extension .*$", r"^layout\(local_size_x.*$", r"^layout\(push_constant\).*$",
        r"^layout\(binding = 16\) uniform accelerationStructureEXT tlas;\s*$",
        r"^layout\(std430, binding = \d+\) (readonly )?buffer \w+ \{ [^}]* \};.*$"]
for d in drop:
    src = re.sub(d, "", src, flags=re.M)
src, n = re.subn(r"^layout\(std430, binding = 0\) readonly buffer Params \{", "struct ParamsBlock {", src, flags=re.M)
assert n == 1, "the Params block"
src, n = re.subn(r"^\} P;", "};", src, flags=re.M)
assert n == 1, "the Params block's end"
leftover = re.findall(r"^\s*layout\(.*$", src, flags=re.M)
assert not leftover, "a layout this translation doesn't know: %s" % leftover[:2]

# --- the intersection: OptiX's
src, n = re.subn(r"^#ifdef USE_RAY_QUERY\nbool intersect\(",
                 "#if 1  // (the OptiX device: optixTrace through the scene's acceleration structures)\n"
                 "bool intersect(vec3 o, vec3 d, inout Hit h) { return optix_intersect(o, d, h.t, h.u, h.v, h.prim, h.obj); }\n"
                 "#elif defined(USE_RAY_QUERY)\nbool intersect(", src, flags=re.M)
assert n == 1, "the intersect() variants"

# --- struct constructors (GLSL's positional ones) as braces
for name in ("ToonIn", "Hit", "Sample", "SurfacePoint"):
    out, i = [], 0
    pat = re.compile(r"\b%s\(" % name)
    while True:
        m = pat.search(src, i)
        if not m:
            out.append(src[i:])
            break
        out.append(src[i:m.start()] + name + "{")
        depth, j = 1, m.end()
        while depth:
            depth += {"(": 1, ")": -1}.get(src[j], 0)
            j += 1
        out.append(src[m.end():j - 1] + "}")
        i = j
    src = "".join(out)

# --- array constructors (int[8](...)) as braces
out, i = [], 0
pat = re.compile(r"\b(?:int|uint|float|vec2|vec3|vec4)\[\d+\]\(")
while True:
    m = pat.search(src, i)
    if not m:
        out.append(src[i:])
        break
    out.append(src[i:m.start()] + "{")
    depth, j = 1, m.end()
    while depth:
        depth += {"(": 1, ")": -1}.get(src[j], 0)
        j += 1
    out.append(src[m.end():j - 1] + "}")
    i = j
src = "".join(out)

# --- the buffers by their own names (B_<name>): a local may share a buffer's name (GLSL lets it shadow), a macro can't
BUFFERS = ["accum", "aux", "nodes", "tris", "insts", "verts", "idx", "mats", "lights", "mlights", "mcdf", "tex8", "texf", "texinfo", "tlas_obj"]
for name in BUFFERS:
    assert not re.search(r"\b(?:int|uint|float|bool|vec[234]|ivec[234]|uvec[234])\s+%s\s*\[" % name, src), "a local array named like the buffer " + name
    src = re.sub(r"\b%s\[" % name, "B_%s[" % name, src)
src = re.sub(r"\bP\.", "B_P.", src)
src = re.sub(r"\bPC\.", "B_PC.", src)

# --- out / inout parameters as references (in a parameter list: after "(" or ", ")
src, n = re.subn(r"(?<=[(,]\s)(?:out|inout) (\w+) (\w+)", r"\1 &\2", src)
src, n2 = re.subn(r"(?<=\()(?:out|inout) (\w+) (\w+)", r"\1 &\2", src)
assert not re.search(r"[(,]\s*(out|inout) \w+ \w+", src), "an out / inout parameter left"

# --- the swizzles: the one write, then the reads
src, n = re.subn(r"\b(\w+(?:\.\w+)*)\.xyz = (.+?);", r"\1.set_xyz(\2);", src)
src = re.sub(r"\.(xyz|xy)\b(?!\s*\()", r".\1()", src)

assert not re.search(r"\.(xyz|xy)\(\)\s*[-+*/]?=[^=]", src), "a swizzle write this translation doesn't know (it would compile and do nothing)"

# --- float literals
src = re.sub(r"(?<![\w.])(\d+\.\d*|\.\d+|\d+(?=[eE]))([eE][-+]?\d+)?(?![\w.])", lambda m: m.group(0) + "f", src)

# --- main
src, n = re.subn(r"^void main\(\) \{", "void run() {", src, flags=re.M)
assert n == 1, "main()"

# --- every function inlined, as the GLSL compiler does: a call left out of line passes its structs (a Material is 272
# bytes) through local memory and spills what is live round it, and under OptiX that is the continuation stack
FUNC = re.compile(r"^(?!return\b|if\b|else\b|for\b|while\b|struct\b|const\b|#)([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\(([^;]*)\)\s*\{", re.M)
src, n = FUNC.subn(lambda m: "__forceinline__ " + m.group(0), src)
assert n > 40, "the functions to inline: %d" % n

header = """// Generated by tools/glsl_to_cuda.py from src/render/gpu_kernel.h and gpu_glsl.h (task 0170): the GPU path
// tracer's kernel as CUDA, for the OptiX device. Do not edit: change the GLSL and run tools/optix_ptx_embed.bat.
struct GlslKernel : GlslBase {
"""
open(OUT, "w", encoding="utf-8", newline="\n").write(header + src + "\n};\n")
print("glsl_to_cuda: %d lines" % (src.count("\n") + 4))
