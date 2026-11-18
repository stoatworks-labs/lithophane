"""The demo's shaders must be the plugin's shaders, character for character.

    python3 demo/tools/check_shaders.py            compare; exit 1 on any drift
    python3 demo/tools/check_shaders.py --write    regenerate demo/shaders.js
    python3 demo/tools/check_shaders.py --hashes   print the ported C++'s hashes (for a reader)

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.

------------------------------------------------------------------- why

`demo/shaders.js` holds the GLSL pieces of `source/Shaders.cpp`: kVersion, the
full-raster triangle (kQuadVertex), kCommon (sRGB both ways, the NaN tame, the
quadratic B-spline and the weight table read by thickness) and the four
fragment passes -- carve, spread, pass and composite. That is two copies of the
same text, and two copies drift -- quietly, because a plate made by a
*plausible* shader looks exactly like one made by the right one. The whole
claim of the page is that it runs the plugin's own shaders, so the claim needs
something enforcing it. `lptest` drives the real plugin class and has never
heard of this page, and `tools/glslc.sh` compiles the C++ copies and never
looks at the JS one.

------------------------------------------------------------------- what it does

Pulls each piece out of `source/Shaders.cpp` and the matching backtick literal
out of `demo/shaders.js`, and compares them exactly -- no whitespace
normalisation, no comment stripping: a comment updated on one side only is
exactly the drift worth catching. `kVersion`, a plain string, is compared too.
A piece may be several adjacent raw strings; this joins them as the compiler
does, with nothing between.

It fails when Shaders.cpp grows a piece this script does not list (a pass the
page would silently lack), and it checks the JOIN as well as the pieces:
Assemble() is kVersion + kCommon + the pass for each of the four fragment
stages and AssembleVertex() is kVersion + kQuadVertex for all of them,
Lithophane.cpp's InitGL compiles them in the order carve, spread, pass,
composite, and demo/port.js must assemble them the same way. The constants the
GLSL and the C++ share (kKernelCentre, kKernelTaps, kMaxLevels) are pinned on
both sides too.

The page is more than the shaders. The CPU half -- Plate.cpp's materials, the
multipole profile, the 15-Gaussian NNLS fit, the kernels and the frame's plan;
Controls.cpp's table and conversions; Lithophane.cpp's ProcessOpenGL, render,
ensureBuffers and setKernels; MultiTarget's targets -- is translated by hand
into demo/plate.js and demo/port.js. This cannot check a translation, but it
can trip when its source moves: PORTED_FROM holds the SHA-256 of every C++ file
and function the port was made from, and a change to any of them fails here
until a reader carries it across and updates the hash (`--hashes` prints the
new ones). demo/tools/crosscheck/ is what checks the translation itself.

And it fails when a piece declares a uniform that demo/port.js never names (a
uniform the page does not set sits at 0, a control quietly dead on the page and
live in the plugin), or when port.js hands an integer uniform -- int, ivec,
uint or a sampler -- to the kit's `.set()`, which always uploads a float and so
is refused by GL without a word.

The one transformation is a decode, not a normalisation: a backtick cannot
appear raw inside a template literal, so --write would escape one as \\`.
Shaders.cpp has none today. This undoes that escape and REJECTS any other
backslash or any `${` on the JS side; there are none in the C++ bodies, so
either could only be somebody hiding a difference.

------------------------------------------------------------------- what it cannot

Nothing here checks the PORTED half itself. demo/tools/crosscheck/port_numbers.py
compares the port's numbers with the C++'s (compiled on the plugin's own
Plate.cpp and Controls.cpp), and crosscheck.py renders lptest --pipe and the
page from the same frames and compares the pictures; both need node, Chrome
and a GPU, so they are run by hand, not by verify.sh. When you change the C++
the port came from, change demo/plate.js or demo/port.js too.
"""
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

# JS constant, C++ symbol (all in source/Shaders.cpp), in the order they are
# declared there. kVersion, a plain string, is handled on its own.
SHADERS = [
    ("QUAD_VERTEX", "kQuadVertex"),
    ("COMMON", "kCommon"),
    ("CARVE_FRAGMENT", "kCarveFragment"),
    ("SPREAD_FRAGMENT", "kSpreadFragment"),
    ("PASS_FRAGMENT", "kPassFragment"),
    ("COMPOSITE_FRAGMENT", "kCompositeFragment"),
]

# How the pieces are joined, on each side, and the constants both sides
# share. Each entry is (file, text that must appear in it verbatim).
ASSEMBLY = [
    ("source/Shaders.cpp",
     "std::string AssembleVertex( Pass )\n{\n"
     "\treturn std::string( kVersion ) + kQuadVertex;\n}"),
    ("source/Shaders.cpp",
     "\tcase Pass::Carve: return std::string( kVersion ) + kCommon + kCarveFragment;\n"
     "\tcase Pass::Spread: return std::string( kVersion ) + kCommon + kSpreadFragment;\n"
     "\tcase Pass::Pass: return std::string( kVersion ) + kCommon + kPassFragment;\n"
     "\tcase Pass::Composite: return std::string( kVersion ) + kCommon + kCompositeFragment;\n"),
    ("source/Shaders.h", "enum class Pass\n{\n\tCarve,\n\tSpread,\n\tPass,\n\tComposite\n};"),
    ("source/Shaders.h", "constexpr int kKernelCentre = 20;"),
    ("source/Shaders.h", "constexpr int kKernelTaps   = 2 * kKernelCentre + 1;"),
    ("source/Shaders.h", "constexpr int kMaxLevels = 8;"),
    ("source/Lithophane.cpp",
     '\t\t{ &carveShader, shaders::Pass::Carve, "carve" },\n'
     '\t\t{ &spreadShader, shaders::Pass::Spread, "spread" },\n'
     '\t\t{ &passShader, shaders::Pass::Pass, "pass" },\n'
     '\t\t{ &compositeShader, shaders::Pass::Composite, "composite" },\n'),
    ("source/Lithophane.cpp", "const std::string vertex   = shaders::AssembleVertex( stage.pass );"),
    ("source/Lithophane.cpp", "const std::string fragment = shaders::Assemble( stage.pass );"),
    ("source/Lithophane.cpp", "if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )"),
    ("demo/port.js", "export const PASSES = ['carve', 'spread', 'pass', 'composite'];"),
    ("demo/port.js", "export const kKernelCentre = 20;"),
    ("demo/port.js", "export const kKernelTaps = 2 * kKernelCentre + 1;"),
    ("demo/port.js", "export const kMaxLevels = 8;"),
    ("demo/port.js",
     "function AssembleVertex() {\n"
     "  return S.VERSION + S.QUAD_VERTEX;\n}"),
    ("demo/port.js",
     "    carve: S.VERSION + S.COMMON + S.CARVE_FRAGMENT,\n"
     "    spread: S.VERSION + S.COMMON + S.SPREAD_FRAGMENT,\n"
     "    pass: S.VERSION + S.COMMON + S.PASS_FRAGMENT,\n"
     "    composite: S.VERSION + S.COMMON + S.COMPOSITE_FRAGMENT,\n"),
    ("demo/port.js",
     "programs[pass] = new Program(gl, AssembleVertex(pass), Assemble(pass), pass, { attribs: {} });"),
]

# What demo/plate.js and demo/port.js were translated from, by SHA-256: whole
# files, or one C++ function from its signature to its closing brace. Not a
# proof of the port: a tripwire for its source. Run --hashes after carrying a
# change across.
PORTED_FROM = [
    ("source/Plate.h", None, "c4ae0eaf46b5aee013bb60a1e775e53c277c1a8ad7e1671874aefa26c7f615cd"),
    ("source/Plate.cpp", None, "46a6c3a1b9d2c271bdba1fbce7089737cff3ff934cd749405e65dccb1c59aa87"),
    ("source/Controls.h", None, "b7b687c98420d4b42b0c97cb26fc790668929a2d1d205885a8d6ce75f627b498"),
    ("source/Controls.cpp", None, "29b6b8f6966c0d3eee9df8bc85c670475277fecff34038b7192ab55b8534d655"),
    ("source/MultiTarget.cpp", "bool MultiTarget::Ensure( int requestedWidth, int requestedHeight, int requestedCount, GLint requestedFormat )", "52a3754e536a268723dd7d225aa657d000eb8ec91f98954df77bcfa5fb1eaa1e"),
    ("source/Lithophane.cpp", "LithophanePlugin::LithophanePlugin()", "7da573c89b8ab6246c7fc2dc76c2fbbce17b29433c8a02004f1e61557f6c4f28"),
    ("source/Lithophane.cpp", "FFResult LithophanePlugin::InitGL( const FFGLViewportStruct* vp )", "0065341096305b1f51017639cd4fac0de62ab57afa2bd042d3ac4676c15a0a22"),
    ("source/Lithophane.cpp", "bool LithophanePlugin::ensureBuffers( int width, int height )", "06702a3bb9cdcef3afd73d00faeb23cf72ad21121c056ce0b0ee1e17069749b4"),
    ("source/Lithophane.cpp", "void LithophanePlugin::setKernels( const FFGLShader& shader, const plate::Group& group )", "b1db859bc1a5594e1f8351149a826f2c02edea8897d7006ed743de53cef8b3c1"),
    ("source/Lithophane.cpp",
     "void LithophanePlugin::render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, "
     "int width, int height )", "f20fee86576203924eecbf5f81c85aa714e0ebd39b6e7355cf06ea51ed7736f3"),
    ("source/Lithophane.cpp", "FFResult LithophanePlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )", "4b9dcb0954b95aefbe3a36c84f215cb4cf1fecaeb3a30687020adbd1a97c3f6c"),
    ("source/Lithophane.cpp", "FFResult LithophanePlugin::SetFloatParameter( unsigned int index, float value )", "c4a6c4c3225fd70a0fccb4b422aa01d4c791a3bc261bdd923a70e94b5b2a3a2d"),
    # The hooks' defaults: every negative control off, which is what the page
    # sets for the model uniforms (AttenuationModel 0, Premultiply 1, ...).
    ("source/Lithophane.h", "struct PluginHooks", "2f0793ce554d2520e24e6664b75f886dea0bd6e6324cde96d1dc92867dfc1f68"),
]

# The pieces whose uniforms port.js must set (by name, quoted).
UNIFORM_PIECES = [symbol for _, symbol in SHADERS]

HEADER = """// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape allowed is \\` for a backtick inside a comment.
"""


def read(*parts):
    with open(os.path.join(REPO, *parts), "r", encoding="utf-8") as handle:
        return handle.read()


def cpp_version(source):
    match = re.search(r'const char\* const kVersion = "(.*?)";', source)
    return None if match is None else match.group(1)


def from_cpp(source, symbol):
    """The piece's text: its raw strings, joined as the compiler joins them.

    Returns (text, count of raw strings), or (None, 0) if the symbol is not
    there or what follows its `=` is not a run of adjacent raw strings ending
    in `;`.
    """
    match = re.search(r'(?:static )?const char\* const ' + symbol + r' = ((?:R"\(.*?\)"\s*)+);', source, re.S)
    if match is None:
        return None, 0
    parts = re.findall(r'R"\((.*?)\)"', match.group(1), re.S)
    rebuilt = re.sub(r'R"\(.*?\)"', "", match.group(1), flags=re.S)
    if rebuilt.strip() != "":
        return None, 0
    return "".join(parts), len(parts)


def cpp_pieces(source):
    """Every raw-string piece Shaders.cpp defines, by symbol."""
    return re.findall(r'(?:static )?const char\* const (k\w+) = R"\(', source)


def from_js(source, name):
    match = re.search(r'^export const ' + name + r' = `(.*?)`;$', source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?!`)", body)
    if stray is not None:
        line = body[: stray.start()].count("\n") + 1
        return None, f"backslash that is not an escaped backtick, at line {line}"
    if "${" in body:
        return None, "template substitution"
    return body.replace("\\`", "`"), None


def uniforms(glsl):
    """Every uniform a piece declares, as (type, name), comments stripped
    first. An array counts by its name."""
    code = re.sub(r"//[^\n]*", "", glsl)
    return re.findall(r"^\s*uniform\s+(\w+)\s+(\w+)\s*(?:\[\s*\d+\s*\])?\s*;", code, re.M)


INTEGER_TYPES = re.compile(r"^(int|uint|ivec[234]|uvec[234]|bool|bvec[234]|[iu]?sampler\w+)$")


# ---------------------------------------------------------------- the port's source
def function_body(source, signature):
    """From `signature` to the brace that closes the body after it."""
    start = source.find(signature)
    if start < 0:
        return None
    brace = source.find("{", start + len(signature))
    if brace < 0:
        return None
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    return None


def ported_hashes():
    out = []
    for path, signature, want in PORTED_FROM:
        try:
            text = read(*path.split("/"))
        except FileNotFoundError:
            out.append((path, signature, want, None))
            continue
        piece = text if signature is None else function_body(text, signature)
        got = None if piece is None else hashlib.sha256(piece.encode("utf-8")).hexdigest()
        out.append((path, signature, want, got))
    return out


def check_ported():
    problems = 0
    for path, signature, want, got in ported_hashes():
        what = path if signature is None else f"{path}: {signature.split('(')[0]}"
        if got is None:
            print(f"FAIL  ported from {what}: not found")
            problems += 1
        elif got != want:
            print(f"FAIL  ported from {what}: changed since the port was made from it "
                  f"(sha256 {got[:12]}, the port's {want[:12]}) -- carry the change across, then --hashes")
            problems += 1
        else:
            print(f"ok    ported from {what} ({got[:12]})")
    return problems


# ---------------------------------------------------------------- shaders.js
def write(cpp):
    out = [HEADER]
    version = cpp_version(cpp)
    if version is None:
        print("FAIL  kVersion not found in source/Shaders.cpp")
        return 1
    # kVersion is "#version 410 core\n" in the C++, the \n an escape there.
    out.append(f"export const VERSION = '{version}';\n")
    for name, symbol in SHADERS:
        body, count = from_cpp(cpp, symbol)
        if body is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp (or not a run of adjacent raw strings)")
            return 1
        if "\\" in body or "${" in body:
            print(f"FAIL  {symbol} holds a backslash or ${{; the escape scheme cannot carry it")
            return 1
        joined = f", {count} adjacent raw strings joined" if count > 1 else ""
        out.append(f"\n// {symbol}, source/Shaders.cpp{joined}\nexport const {name} = `{body.replace('`', chr(92) + '`')}`;\n")
    with open(os.path.join(REPO, "demo", "shaders.js"), "w", encoding="utf-8") as handle:
        handle.write("".join(out))
    print("wrote demo/shaders.js")
    return 0


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


def verbatim(entries, what):
    problems = 0
    for path, text in entries:
        try:
            body = read(*path.split("/"))
        except FileNotFoundError:
            print(f"FAIL  {path} is missing")
            problems += 1
            continue
        if text in body:
            print(f"ok    {what}: {path} still says {text.strip().splitlines()[0][:60]}")
        else:
            print(f"FAIL  {what}: {path} no longer says: {text.strip().splitlines()[0]}")
            problems += 1
    return problems


def check(cpp, js):
    problems = 0
    version_cpp = cpp_version(cpp)
    version_js = re.search(r"^export const VERSION = '(.*?)';$", js, re.M)
    if version_cpp is None or version_js is None or version_cpp != version_js.group(1):
        print("FAIL  VERSION does not match kVersion")
        problems += 1
    else:
        print(f"ok    {'VERSION':<20} matches kVersion")

    listed = {symbol for _, symbol in SHADERS}
    for symbol in cpp_pieces(cpp):
        if symbol not in listed:
            print(f"FAIL  source/Shaders.cpp defines {symbol}, which this check does not list -- "
                  "add it to SHADERS (and to the page, or say on the page why not)")
            problems += 1

    for name, symbol in SHADERS:
        cpp_text, count = from_cpp(cpp, symbol)
        js_text, complaint = from_js(js, name)
        if cpp_text is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp (or not a run of adjacent raw strings)")
            problems += 1
        elif complaint is not None:
            print(f"FAIL  {name} in demo/shaders.js has a {complaint}")
            problems += 1
        elif js_text is None:
            print(f"FAIL  {name} not found in demo/shaders.js")
            problems += 1
        elif cpp_text == js_text:
            joined = f", {count} raw strings joined" if count > 1 else ""
            print(f"ok    {name:<20} matches {symbol} ({len(cpp_text)} chars{joined})")
        else:
            problems += 1
            print(f"FAIL  {name} has drifted from {symbol}")
            where = first_difference(cpp_text, js_text)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          C++: {where[1]}")
                print(f"          js : {where[2]}")

    problems += verbatim(ASSEMBLY, "assembly")
    problems += check_ported()

    try:
        port = read("demo", "port.js")
    except FileNotFoundError:
        port = ""

    for symbol in UNIFORM_PIECES:
        text, _ = from_cpp(cpp, symbol)
        if text is None:
            continue
        declared = uniforms(text)
        unset = [n for _, n in declared if f"'{n}'" not in port]
        floated = [n for t, n in declared
                   if INTEGER_TYPES.match(t) and re.search(r"\.set\(\s*'" + n + r"'", port)]
        if unset:
            print(f"FAIL  {symbol} declares {', '.join(unset)}, which demo/port.js never names -- "
                  "the page would leave it at 0")
            problems += 1
        elif floated:
            print(f"FAIL  {symbol}'s integer uniform(s) {', '.join(floated)} go through .set(), which uploads a "
                  "float that GL refuses -- use setInt, setSampler, gl.uniform2i or gl.uniform4i")
            problems += 1
        elif declared:
            print(f"ok    uniforms: all {len(declared)} of {symbol}'s are named in demo/port.js, integers as integers")
    return problems


def main(argv):
    cpp = read("source", "Shaders.cpp")
    if "--write" in argv:
        return write(cpp)
    if "--hashes" in argv:
        for path, signature, _, got in ported_hashes():
            print(f"{got}  {path}{'' if signature is None else ': ' + signature}")
        return 0
    try:
        js = read("demo", "shaders.js")
    except FileNotFoundError:
        print("FAIL  demo/shaders.js is missing -- run demo/tools/check_shaders.py --write")
        return 1
    problems = check(cpp, js)
    print()
    if problems:
        print(f"{problems} problem(s) -- rerun demo/tools/check_shaders.py --write for a drifted shader copy "
              "(never edit shaders.js by hand); an assembly, ported-source, uniform or unlisted-piece failure "
              "needs a reader")
        return 1
    print(f"all {len(SHADERS) + 1} shader pieces, their assembly, the shared constants, the ported C++ and the "
          "uniforms are the plugin's")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
