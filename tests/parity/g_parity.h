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

// True if -framehash was supplied. In frame-hash mode the caller (D_DoomMain)
// forces singletics so gametic == displayed-frame index, making the sampled
// frames deterministic.
int G_ParityFrameHashEnabled(void);

// Sample the indexed framebuffer screens[0] if the current gametic is one of
// the fixed target tics and no wipe is in progress. Called from D_Display just
// before the final blit. Inert unless -framehash was supplied.
void G_ParityFrameSample(int wipe_in_progress);

// ---- Phase 5: netgame loopback instrumentation (inert unless flagged) ------

#include "d_ticcmd.h"

// True if -scriptcmds <file> was supplied.
int G_NetScriptEnabled(void);

// Overwrite the local player's control fields from the scripted stream,
// preserving cmd->consistancy. Called at the end of G_BuildTiccmd.
void G_NetScriptApply(ticcmd_t* cmd);

// If -exittic <N> was supplied and gametic has reached N, emit the world-state
// checksum and exit. Called from TryRunTics right after gametic++.
void G_ParityExitTicCheck(void);

#endif
