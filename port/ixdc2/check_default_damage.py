"""Offline donor-free object equivalence using an existing build's compiler flags."""
import argparse
import json
from pathlib import Path
import runpy
import shlex
import subprocess

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    args.output.mkdir(parents=True, exist_ok=True)
    commands = json.loads((args.build / "compile_commands.json").read_text())
    function = runpy.run_path(str(Path(__file__).with_name("test_retail_damage.py")))["function"]
    for filename, retail_file, signature, symbol in (
        ("ridepod_damage", "monstorunit", "int CMonstorUnit::CheckDmg()", "_ZN12CMonstorUnit8CheckDmgEv"),
        ("ridepod_records", "collisiondata", "int CCollisionData::Set(", "_ZN14CCollisionData3SetEPfiiffiiii"),
        ("ridepod_scene", "monstorunit", "int CMonstorUnit::SetupViewMonstor(", "_ZN12CMonstorUnit16SetupViewMonstorEiPfi"),
    ):
        source = root / f"port/src/{filename}.cpp"
        entry = next(c for c in commands if Path(c["file"]).resolve() == source.resolve())
        flags = [x.strip('"') for x in shlex.split(entry["command"], posix=False)]
        compiler = Path(flags[0])
        flags = flags[1:]
        for option in ("-o", "-c"):
            index = flags.index(option)
            del flags[index:index + 2]
        flags = [x for x in flags if not x.startswith("-DIXION_HAS_DC2_RIDEPOD")]
        flags.append(f"-I{root / 'port/src'}")
        current = source.read_text()
        start = current.index("PORT_OVERRIDE " + signature)
        includes = current[:start]
        declaration = current[start:current.index("{",start)]
        # Records includes contain sidecar functions inside the donor enclosure;
        # that enclosure is absent from the donor-free compile.
        reference = args.output / f"{filename}-retail.cpp"
        reference.write_text(includes + declaration +
                             function((root / f"ps2/src/{retail_file}.cpp").read_text(), signature) + "\n")
        listings = []
        for label, src in (("private", source), ("retail", reference)):
            obj = args.output / f"{filename}-{label}.obj"
            result = subprocess.run([str(compiler), *flags, "-o", str(obj), "-c", str(src)],
                                    cwd=entry["directory"], capture_output=True, text=True)
            (args.output / f"{filename}-{label}-compile.log").write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(f"{label} compile failed: {result.stderr}")
            listing = subprocess.check_output([str(compiler.with_name("llvm-objdump.exe")), "-dr",
                                                f"--disassemble-symbols={symbol}", str(obj)], text=True)
            (args.output / f"{filename}-{label}-assembly.txt").write_text(listing)
            # The object path is the only irrelevant difference. Retain every
            # instruction byte, relocation target and addend for comparison.
            listings.append(listing[listing.index("Disassembly of section"):])
        if listings[0] != listings[1] or f"<{symbol}>" not in listings[0]:
            raise AssertionError(f"donor-free {symbol} differs from retail")
        print(f"PASS donor-free {symbol}: instruction bytes and relocations identical to retail")

if __name__ == "__main__":
    main()
