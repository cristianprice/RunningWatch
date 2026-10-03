"""Link the embedded assets that PlatformIO generates but forgets to build.

`board_build.embed_files` makes PlatformIO run ESP-IDF's embed script, which
turns each asset into `$BUILD_DIR/<name>.S` carrying the usual
`_binary_<name>_start` / `_binary_<name>_end` symbols. On the espidf framework
it then only declares a dependency on that assembly instead of compiling it, so
the symbols never reach the linker. Assembling it here and putting the object
straight on the link line closes the gap.
"""

from os.path import basename

Import("env")

PROGRAM = "$BUILD_DIR/${PROGNAME}.elf"

for asset in env.GetProjectOption("board_build.embed_files", "").splitlines():
    asset = asset.strip()
    if not asset:
        continue

    name = basename(asset)
    embedded = env.Object(
        target="$BUILD_DIR/%s.embed.o" % name, source="$BUILD_DIR/%s.S" % name
    )

    env.Depends(PROGRAM, embedded)
    env.Append(LINKFLAGS=[embedded[0].get_abspath()])
