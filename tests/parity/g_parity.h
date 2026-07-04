// Phase 2 modernization: demo-parity oracle hook.
//
// Provides a deterministic, canonical world-state checksum used to freeze and
// verify the self-frozen golden master (see docs/oracle/ORACLE_STRATEGY.md).
// This is instrumentation only: it is inert unless -checkdemo is passed, so
// normal play/record/replay behaviour is unchanged.
//
// Activated with:   -checkdemo            print PARITY_CHECKSUM <hex>, exit(0)
//                   -parityref <file>     also compare against a frozen ref;
//                                         exit(0) on match, exit(3) on mismatch.

#ifndef __G_PARITY__
#define __G_PARITY__

// True if -checkdemo was supplied on the command line.
int G_ParityEnabled(void);

// Compute the canonical world-state checksum, print it, optionally compare
// against -parityref, and exit the process. Called at demo completion. Does
// nothing (returns) if -checkdemo was not supplied.
void G_ParityCheckAndExit(void);

#endif
