# Marbles - 4k

4k intro released at [Revision 2026](https://2026.revision-party.net/).

## Links

- [Pouet](TODO)
- [Demozoo](TODO)
- [YouTube](TODO)

## Build

Building the intro requires the following tools to be installed and accessible via a
PowerShell command line (e.g. in PATH):

- Microsoft Visual C++ compiler `cl.exe` and linker `link.exe` for x86
- [`crinkler`](https://github.com/runestubbe/Crinkler)
- [`shader_minifier`](https://github.com/laurentlb/Shader_Minifier)

If you are using an antivirus software (Windows Defender included),
it is likely to detect the compiled executable as
a [trojan](https://en.wikipedia.org/wiki/Trojan_horse_(computing)).
You'll need to whitelist the file or the entire folder in your antivirus before compiling or running it.

Build an uncompressed debug version (uses MSVC's linker):

```powershell
.\build.ps1
```

or the compressed release (uses crinkler):

```powershell
.\build.ps1 .\release.json -CrinklerTries 3000 -XRes 1920 -YRes 1080
```

and run:

```powershell
.\main.exe
```