#@runtime Jython
"""Where a Ghidra analysis script writes its output.

These scripts used to write to an absolute path inside the game INSTALL
directory. That was wrong twice over: it hardcoded one machine's layout into a
tracked file, and it wrote run artifacts into the untouched game install, which
this project treats as read-only (run directories are symlink farms over it).

Output goes to the repo's gitignored `scratch/logs/` instead. The location is
resolved in this order:

  1. `$X2_SCRATCH_LOGS`, if set -- the explicit override;
  2. `<repo>/scratch/logs`, where <repo> is derived from this file's own
     location (tools/ghidra_scripts/x2out.py -> ../../);
  3. failing both, it RAISES rather than silently dropping the file somewhere
     arbitrary. A script whose output vanished into the wrong directory reads
     exactly like a script that found nothing.

Ghidra puts the script's own directory on sys.path, so `from x2out import
outpath` works from any script beside it.
"""
import os


def logdir():
    d = os.environ.get("X2_SCRATCH_LOGS")
    if not d:
        here = None
        try:
            here = os.path.dirname(os.path.abspath(__file__))
        except NameError:
            pass
        if not here:
            raise RuntimeError(
                "x2out: cannot locate the repository -- __file__ is unset in "
                "this interpreter and $X2_SCRATCH_LOGS is not set. Set "
                "X2_SCRATCH_LOGS to the directory output should go to; "
                "refusing to guess.")
        repo = os.path.dirname(os.path.dirname(here))
        d = os.path.join(repo, "scratch", "logs")
    if not os.path.isdir(d):
        os.makedirs(d)
    return d


def outpath(name):
    """Absolute path for an output file called `name`, dir created."""
    return os.path.join(logdir(), name)
