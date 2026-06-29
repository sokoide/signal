# Normalize volatile output values for deterministic comparison.
# Consumed by check-linux.sh with GNU sed (`sed -i -f`), inside the Linux VM.
#
# Volatile sources:
#   - PIDs / UIDs (process- and run-dependent)
#   - addresses dumped from ucontext_t / si_addr (ASLR, arch pointer width)
#   - CPU-speed-dependent timer counts (04 busy loop)

# PIDs and UIDs in the forms the samples print them.
s/PID: [0-9][0-9]*/PID: <PID>/g
s/PID=[0-9][0-9]*/PID=<PID>/g
s/si_pid=[0-9][0-9]*/si_pid=<PID>/g
s/si_uid=[0-9][0-9]*/si_uid=<UID>/g
s/ppid=[0-9][0-9]*/ppid=<PID>/g
# 07 prints "kill -INT <pid>" / "kill -TERM <pid>" instructions.
s/kill -INT [0-9][0-9]*/kill -INT <PID>/g
s/kill -TERM [0-9][0-9]*/kill -TERM <PID>/g

# Hex addresses (8+ hex digits) — 02 si_addr, 08 saved PC/SP/FP.
s/0x[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]*/0x<ADDR>/g

# 04: VIRTUAL timer fires N times during the busy loop — N depends on CPU speed.
s/fired [0-9][0-9]* times during busy loop/fired <N> times during busy loop/g
