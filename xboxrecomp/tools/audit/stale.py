#!/usr/bin/env python3
"""Stop leftover *test* instances of the game -- never the player's.

The audit tools used to start every run with `taskkill /IM "SSX Tricky.exe"
/F`, which also closed a game the user had started themselves (from Explorer
or the launcher) whenever a test began -- it looked to them like the game
dying during its hard-disk check (part 183). A test instance is one whose
parent is a script host (python, bash, timeout, cmd, powershell); the
player's is started by Explorer, a shortcut or the launcher's own process.

    from stale import kill_test_instances
    kill_test_instances()
"""
import subprocess

EXE = "SSX Tricky.exe"
TEST_PARENTS = ("python", "py.exe", "pythonw", "bash", "sh.exe", "timeout",
                "cmd.exe", "powershell", "pwsh", "mintty", "conhost")


def _processes():
    """[(pid, parent_pid, name)] for every process, via PowerShell/CIM."""
    ps = ("Get-CimInstance Win32_Process | "
          "ForEach-Object { \"$($_.ProcessId)`t$($_.ParentProcessId)`t$($_.Name)\" }")
    out = subprocess.run(["powershell", "-NoProfile", "-Command", ps],
                         capture_output=True, text=True).stdout
    rows = []
    for line in out.splitlines():
        parts = line.split("\t")
        if len(parts) == 3 and parts[0].isdigit() and parts[1].isdigit():
            rows.append((int(parts[0]), int(parts[1]), parts[2]))
    return rows


def kill_test_instances():
    rows = _processes()
    names = {pid: name.lower() for pid, _, name in rows}
    for pid, ppid, name in rows:
        if name.lower() != EXE.lower():
            continue
        parent = names.get(ppid, "")
        if parent and not any(parent.startswith(t) for t in TEST_PARENTS):
            continue            # started by Explorer / a shortcut: the player's
        subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True)


if __name__ == "__main__":
    kill_test_instances()
