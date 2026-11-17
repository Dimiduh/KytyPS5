# ooz_decode

A small program of its own that unpacks Oodle streams (Kraken, Mermaid, Selkie, Leviathan). The
emulator's precompile pass starts it to read the shaders out of an Unreal Engine game's files
(`shader-seeds/README.md`, `src/graphics/shader/gameShaderSeeds.h`).

```
ooz_decode <input file> <output file>
```

- Input: `OOZDEC1` and a zero byte, then one record per stream: packed size and raw size (32 bits
  each, little endian) and the packed bytes.
- Output: the raw bytes of every stream, one after the other.
- Exit code 0 when every stream unpacked to its raw size.

## License

`ooz_decode` is licensed under the GNU General Public License, version 3 or (at your option) any
later version (`LICENSES/GPL-3.0-or-later.txt`). That is another license than the emulator's
(GPL-2.0), which is why it is a separate program: the two share no code and are not linked; the
emulator writes a file, starts `ooz_decode`, and reads the file it wrote.

It is made of:

- `main.cpp` in this folder;
- the ooz decompressor by Powzix, <https://github.com/powzix/ooz>, commit
  `05038060aa68f9187ae9923b2388ca8db40e58d1`, files `kraken.cpp`, `lzna.cpp`, `bitknit.cpp` and
  their headers, unchanged (GPL-3.0-or-later, see the notice at the top of `kraken.cpp`). The
  build fetches them (`3rdparty/CMakeLists.txt`); `-DKYTY_OOZ_DECODE=OFF` leaves the program out.

The complete source of a released `ooz_decode` is therefore this folder at the release's commit
plus ooz at the commit above. A Windows release package carries both in its folder
`ooz_decode-source`.

By hand, with the ooz files next to `main.cpp` (clang-cl or cl, in a Visual Studio command
prompt):

```
clang-cl -c -O2 -EHsc -MD -W0 -Dmain=ooz_cli_main -FIsys/stat.h kraken.cpp lzna.cpp bitknit.cpp
clang-cl -O2 -EHsc -MD -D_CRT_SECURE_NO_WARNINGS main.cpp kraken.obj lzna.obj bitknit.obj -Feooz_decode.exe
```

(`kraken.cpp` also holds ooz's own command line tool: the first option gives its `main` another
name, the second includes a header that tool needs.)

ooz is an open-source reimplementation of the decoders, published by its author under the GPL.
It does not check its input for damage, so it is meant for a game's own files only.

Windows only for now: the ooz sources are written for the Microsoft compiler's dialect.
