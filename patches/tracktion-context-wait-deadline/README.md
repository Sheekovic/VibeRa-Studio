# Context registration polling deadline

Original CONTROL-only correction for pinned Tracktion4536d8a. Existing registration,
context exclusion/resynchronization, atomic device clock and callback code are
unchanged. The former200iteration Sleep(1) loop can take~3.1seconds under coarse
Windows scheduling with a manually driven device. It now stops on actual stream
advancement or a monotonic200ms polling budget. A final sleep/scheduling delay
may overshoot; this is not a hard physical-device deadline.

This neither fabricates callbacks nor changes audio buffers, worker deadlines,
graph architecture, native I/O capabilities or seamless-transition requirements.
The separate300s pre-fix complete diagnostic retained50487assertions/299.352s
as explicitly unqualified evidence. A subsequent normal complete diagnostic
retained50493assertions/141.437s. Historical120s source-suite failures remain
failures against that limit. The complete72-stage source harness now matches
the existing packaged300s aggregate allowance; individual callback/operation
budgets remain unchanged. Runtime qualification is separate from those diagnostics.

Authoring/replay: build/mixer-context-wait-fix-draft-20261019;
exact LF forward/reverse replay: build/mixer-context-wait-replay-lf-20261019.
Earlier raw Git replay failed exact-byte checking through CRLF conversion; the
corrected replay uses per-invocation LF configuration without changing global Git
settings. Native runtime/module qualification belongs to STATUS/VERIFICATION and
the Mixer ledger. The manifest records source-only authoring, not runtime proof.
