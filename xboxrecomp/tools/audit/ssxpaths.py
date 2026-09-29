"""Where things live, for every tool in this folder.

Two layouts are in use:

  SSX-Tricky-PC repository         original working tree
  ------------------------         ---------------------
  port/                            ssx_recomp/
  port/src/recomp/gen/             ssx_recomp/src/recomp/gen/
  port/build/                      ssx_recomp/build/
  game_files/default.xbe           Game Data/default.xbe
  docs/notes/                      RE_NOTES/
  _local/  (scratch, ignored)      (the root itself)

The original tree is recognised by its ssx_recomp/ folder (it also has an
unrelated port/ folder of its own), the repository by port/. Import this instead
of spelling a path out, so a tool works in either:

    import ssxpaths
    ssxpaths.GEN, ssxpaths.XBE, ssxpaths.GAME_EXE, ...
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
REPO = (not os.path.isdir(os.path.join(ROOT, "ssx_recomp"))
        and os.path.isdir(os.path.join(ROOT, "port")))

PORT = os.path.join(ROOT, "port" if REPO else "ssx_recomp")
SRC = os.path.join(PORT, "src")
RECOMP = os.path.join(SRC, "recomp")
GEN = os.path.join(RECOMP, "gen")
BUILD = os.path.join(PORT, "build")
GAME_EXE = os.path.join(BUILD, "SSX Tricky.exe")

GAME_FILES = os.path.join(ROOT, "game_files" if REPO else "Game Data")
XBE = os.path.join(GAME_FILES, "default.xbe")

NOTES = os.path.join(ROOT, "docs", "notes") if REPO else os.path.join(ROOT, "RE_NOTES")
FINDINGS = os.path.join(NOTES, "findings.json")

# Scratch output: recompiler work directories, backups, measurement logs.
WORK = os.path.join(ROOT, "_local") if REPO else ROOT
ANALYSIS = os.path.join(WORK, "xboxrecomp_output", "ssx_analysis.json")
BACKUPS = os.path.join(WORK, "recover_batches") if REPO else os.path.join(NOTES, "recover_batches")
LOGDIR = WORK if REPO else NOTES
