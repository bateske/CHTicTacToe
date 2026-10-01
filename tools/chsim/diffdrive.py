"""Differential redraw check for CHTicTacToe (a test tool, not part of the game).

Builds two simulators from the sketch - A as it is, B with
stage::render() forced to redraw everything whenever it draws (the patch is applied to a temp copy, src/ is
untouched) - runs the same chdrive-style script on both and compares the
framebuffers after every rendered frame. Any difference is a pixel the
incremental redraw left stale or failed to update.

    set CHSIM_CXX=...zig.exe c++
    python tools/chsim/diffdrive.py <script> <outdir> [ticks_per_render]

ticks_per_render: 1 = a render every logic tick; 3 = the catch-up of a
slow frame (what chdrive's `wait N` does). Ops: wait tap hold release say
snap label. Prints "DIFF frames a-b [label] max N px, union box ..." per
episode and saves the first frame of each as _A/_B/_D (D = diff in magenta).
"""
import os
import shutil
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
WORK = Path(tempfile.gettempdir()) / "chtt_diffdrive"


def prepare():
    sim = WORK / "sim"
    if sim.exists():
        shutil.rmtree(sim)
    shutil.copytree(REPO / "tools" / "chsim", sim, ignore=shutil.ignore_patterns("build"))
    proj = WORK / "proj" / "CHTicTacToe"
    if proj.exists():
        shutil.rmtree(proj)
    proj.mkdir(parents=True)
    shutil.copy(REPO / "CHTicTacToe.ino", proj)
    shutil.copy(REPO / "config.h", proj)
    shutil.copytree(REPO / "src", proj / "src")
    p = proj / "src" / "render" / "Stage.cpp"
    s = p.read_text()
    old = "    wasMoving = moving;\n"
    assert old in s
    p.write_text(s.replace(old, old + "#ifdef FORCE_FULL\n    full = full || motion;\n#endif\n", 1))
    sys.path.insert(0, str(sim))
    from chsim import build
    a = build(proj, [], out=WORK / "simA.exe")
    b = build(proj, ["FORCE_FULL"], out=WORK / "simB.exe")
    return a, b


EXES = prepare()
from chdrive import SimTransport, Driver, mask_of  # noqa: E402
from fbimage import to_image  # noqa: E402

FB = 8192


class Pair:
    def __init__(self, ticks):
        self.a = Driver(SimTransport(EXES[0]))
        self.b = Driver(SimTransport(EXES[1]))
        self.ticks = ticks
        self.frame = 0
        self.ndiff = 0
        self.episodes = []
        self.cur = None
        self.label = ""

    def both(self, fn):
        return fn(self.a), fn(self.b)

    def compare(self, outdir):
        da, db = self.a.shot(), self.b.shot()
        fa, fb = da[:FB], db[:FB]
        if fa == fb:
            if self.cur:
                self.episodes.append(self.cur)
                self.cur = None
            return
        px = []
        for i in range(FB):
            if fa[i] != fb[i]:
                y, xb = divmod(i, 64)
                if (fa[i] ^ fb[i]) & 0x0F: px.append((xb * 2, y))
                if (fa[i] ^ fb[i]) & 0xF0: px.append((xb * 2 + 1, y))
        xs = [p[0] for p in px]; ys = [p[1] for p in px]
        box = (min(xs), min(ys), max(xs), max(ys))
        import os
        if not self.cur or os.environ.get("DUMPALL"):
            if not self.cur: self.cur = {"start": self.frame, "label": self.label, "max": 0, "boxes": []}
            name = f"f{self.frame:05d}"
            to_image(da, 3).save(outdir / f"{name}_A.png")
            to_image(db, 3).save(outdir / f"{name}_B.png")
            # A diff image: B with differing pixels in magenta.
            im = to_image(db, 1)
            p = im.load()
            for (x, y) in px:
                p[x, y] = (255, 0, 255)
            im.resize((384, 384)).save(outdir / f"{name}_D.png")
        self.cur["end"] = self.frame
        if len(px) > self.cur["max"]:
            self.cur["max"] = len(px)
        self.cur["boxes"].append(box)

    def step(self, n, outdir):
        import os
        while n > 0:
            if os.environ.get("SNAPALL") and int(os.environ.get("SNAPFROM","0")) <= self.frame <= int(os.environ.get("SNAPTO","99999")):
                to_image(self.a.shot(), 3).save(outdir / f"all{self.frame:05d}_A.png")
                to_image(self.b.shot(), 3).save(outdir / f"all{self.frame:05d}_B.png")
            k = min(self.ticks, n)
            self.a.cmd(f"N {k}"); self.b.cmd(f"N {k}")
            n -= k
            self.frame += 1
            self.compare(outdir)

    def say(self, d, args):
        d.t.send(" ".join(args))
        for _ in range(10000):
            line = d.t.readline()
            if line.startswith("OK ") and line[3:].strip().isdigit():
                d.t.send("N 5"); continue
            if line.startswith("OK"):
                break
            if line.startswith("ERR"):
                raise SystemExit("refused " + " ".join(args))

    def run(self, script, outdir):
        outdir = Path(outdir); outdir.mkdir(parents=True, exist_ok=True)
        for d in (self.a, self.b):
            d.handshake(); d.cmd("L1")
        for raw in Path(script).read_text().splitlines():
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            op, *args = line.split()
            if op == "label":
                self.label = " ".join(args); continue
            if op == "wait":
                self.step(int(args[0]), outdir)
            elif op == "tap":
                m = mask_of(args[0])
                self.both(lambda d: d.buttons(m))
                self.step(int(args[1]) if len(args) > 1 else 3, outdir)
                self.both(lambda d: d.buttons(0))
                self.step(1, outdir)
            elif op == "hold":
                m = mask_of(args[0]); self.both(lambda d: d.buttons(m))
            elif op == "release":
                self.both(lambda d: d.buttons(0))
            elif op == "say":
                self.say(self.a, args); self.say(self.b, args)
            elif op == "snap":
                to_image(self.a.shot(), 3).save(outdir / f"{args[0]}_A.png")
            elif op == "ticks":
                self.ticks = int(args[0])
            else:
                raise SystemExit("bad op " + line)
        if self.cur:
            self.episodes.append(self.cur)
        bugs = self.a.t.bugs + self.b.t.bugs
        for b in bugs:
            print("SIMBUG", b)
        for e in self.episodes:
            bx = e["boxes"]
            u = (min(b[0] for b in bx), min(b[1] for b in bx), max(b[2] for b in bx), max(b[3] for b in bx))
            print(f"DIFF frames {e['start']}-{e['end']} [{e['label']}] max {e['max']} px, union box x{u[0]}-{u[2]} y{u[1]}-{u[3]}")
        print(f"{self.frame} renders, {len(self.episodes)} diff episodes")
        self.a.t.close(); self.b.t.close()


if __name__ == "__main__":
    Pair(int(sys.argv[3]) if len(sys.argv) > 3 else 1).run(sys.argv[1], sys.argv[2])
