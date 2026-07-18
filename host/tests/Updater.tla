------------------------------ MODULE Updater ------------------------------
(***************************************************************************)
(* TLA+ model of the OTA updater: the check FSM, the four-step apply        *)
(* pipeline, and what a restart does to an in-progress call and to escrowed *)
(* coins.                                                                   *)
(*                                                                          *)
(* Source: host/updater.c. Two independent state variables with two         *)
(* mutexes -- check_state (:12) and apply_state (:152).                      *)
(*                                                                          *)
(* Unlike EventOrdering.tla, the interesting properties here are not about   *)
(* interleaving. They are about a multi-step pipeline that mutates durable   *)
(* state (the git tree, the build output, the installed binary) with no      *)
(* rollback, and reports success without checking whether the last step      *)
(* worked.                                                                   *)
(***************************************************************************)
EXTENDS Naturals

CONSTANTS MaxEvents, CallCost

VARIABLES
    check,     \* check_state: "idle" | "checking" | "checked"   (updater.c:12)
    verKnown,  \* latest_version[0] != '\0'
    apply,     \* apply_state: "idle" | "applying"                (updater.c:152)
    src,       \* the git working tree: "old" | "new"
    build,     \* build output: "old" | "wiped" | "new"  (make clean wipes it)
    installed, \* what `make install` has put in place: "old" | "new"
    running,   \* the binary systemd is actually running: "old" | "new"
    status,    \* apply_status string, collapsed to its meaning
    inCall,    \* daemon_state->current_state == CALL_ACTIVE
    escrow,    \* daemon_state->inserted_cents
    persisted, \* the persisted state file
    budget

vars == <<check, verKnown, apply, src, build, installed, running,
          status, inCall, escrow, persisted, budget>>

Init ==
    /\ check     = "idle"
    /\ verKnown  = FALSE
    /\ apply     = "idle"
    /\ src       = "old"
    /\ build     = "old"
    /\ installed = "old"
    /\ running   = "old"
    /\ status    = "none"
    /\ inCall    = FALSE
    /\ escrow    = 0
    /\ persisted = 0
    /\ budget    = MaxEvents

Tick == budget > 0 /\ budget' = budget - 1

-----------------------------------------------------------------------------
(***************************************************************************)
(* The check FSM -- updater_check_async, updater.c:96-110.                 *)
(*                                                                          *)
(*   if (check_state == 0) { check_state = 1; ... }                         *)
(*                                                                          *)
(* check_thread_func (:84-91) then sets check_state = 2. NOTHING anywhere   *)
(* in updater.c sets it back to 0 -- grep confirms the only writes are      *)
(* :89 (=2), :100 (=1), :106 (=0, the pthread_create failure path) and      *)
(* :127 (=2). So the transition is a one-way ratchet.                        *)
(***************************************************************************)

CheckStart ==
    /\ Tick
    /\ check # "checking"                  \* updater.c:99 -- the guard (was `== "idle"`)
    /\ check' = "checking"
    /\ UNCHANGED <<verKnown, apply, src, build, installed, running,
                   status, inCall, escrow, persisted>>

CheckFinish ==
    /\ Tick
    /\ check = "checking"
    /\ check' = "checked"
    /\ \E ok \in {TRUE, FALSE} : verKnown' = ok   \* curl may fail (:44, :51, :57)
    /\ UNCHANGED <<apply, src, build, installed, running,
                   status, inCall, escrow, persisted>>

-----------------------------------------------------------------------------
(***************************************************************************)
(* The apply pipeline -- updater_apply, updater.c:226-278. Four ordered     *)
(* shell commands, each of which can fail. There is no rollback at any      *)
(* step, and the return code of the LAST one is never checked (:273).       *)
(***************************************************************************)

ApplyStart ==
    /\ Tick
    /\ apply = "idle"
    /\ apply'  = "applying"
    /\ status' = "pulling"
    /\ UNCHANGED <<check, verKnown, src, build, installed, running,
                   inCall, escrow, persisted>>

(* Step 1: git pull --ff-only. On failure the pipeline returns -1 with the
   tree untouched -- this step alone is clean. *)
PullOk ==
    /\ Tick
    /\ apply = "applying" /\ status = "pulling"
    /\ src' = "new"
    /\ status' = "building"
    /\ UNCHANGED <<check, verKnown, apply, build, installed, running,
                   inCall, escrow, persisted>>

PullFail ==
    /\ Tick
    /\ apply = "applying" /\ status = "pulling"
    /\ apply'  = "idle"
    /\ status' = "err_pull"
    /\ UNCHANGED <<check, verKnown, src, build, installed, running,
                   inCall, escrow, persisted>>

(* Step 2: `make clean && make daemon`. make clean runs FIRST and
   unconditionally, so a build failure leaves no binary behind -- and the tree
   is already at the new commit from step 1. *)
BuildOk ==
    /\ Tick
    /\ apply = "applying" /\ status = "building"
    /\ build'  = "new"
    /\ status' = "installing"
    /\ UNCHANGED <<check, verKnown, apply, src, installed, running,
                   inCall, escrow, persisted>>

BuildFail ==
    /\ Tick
    /\ apply = "applying" /\ status = "building"
    /\ build'  = "wiped"          \* make clean already ran; make daemon did not finish
    /\ apply'  = "idle"
    /\ status' = "err_build"
    /\ UNCHANGED <<check, verKnown, src, installed, running,
                   inCall, escrow, persisted>>

(* Step 3: sudo make install. *)
InstallOk ==
    /\ Tick
    /\ apply = "applying" /\ status = "installing"
    /\ installed' = "new"
    /\ status'    = "restarting"
    /\ UNCHANGED <<check, verKnown, apply, src, build, running,
                   inCall, escrow, persisted>>

InstallFail ==
    /\ Tick
    /\ apply = "applying" /\ status = "installing"
    /\ apply'  = "idle"
    /\ status' = "err_install"
    /\ UNCHANGED <<check, verKnown, src, build, installed, running,
                   inCall, escrow, persisted>>

(* Step 4: sudo systemctl restart daemon.service.
   updater.c:273 calls run_command WITHOUT checking its return value, then
   unconditionally sets apply_status to "Update applied successfully" (:276).
   Modelled faithfully: both outcomes lead to the same reported status.

   The restart is also not coordinated with call state -- nothing in
   web_server_handle_api_update consults web_server_is_in_call() or
   daemon_state->current_state. *)
RestartOk ==
    /\ Tick
    /\ apply = "applying" /\ status = "restarting"
    /\ running'   = installed
    /\ inCall'    = FALSE            \* the process dies; any call goes with it
    /\ escrow'    = persisted        \* restored from the file at daemon.c:1171
    /\ apply'     = "idle"
    /\ status'    = "success"
    /\ UNCHANGED <<check, verKnown, src, build, installed, persisted>>

RestartFail ==
    /\ Tick
    /\ apply = "applying" /\ status = "restarting"
    /\ apply'  = "idle"
    /\ status' = "err_restart"       \* now checked -- updater.c:273
    /\ UNCHANGED <<check, verKnown, src, build, installed, running,
                   inCall, escrow, persisted>>

-----------------------------------------------------------------------------
(* Ordinary phone activity, so the restart has something to interrupt. *)

InsertCoin ==
    /\ Tick
    /\ ~inCall
    /\ escrow' = escrow + CallCost
    /\ persisted' = escrow + CallCost      \* daemon_save_state after each coin
    /\ UNCHANGED <<check, verKnown, apply, src, build, installed, running,
                   status, inCall>>

StartCall ==
    /\ Tick
    /\ ~inCall /\ escrow >= CallCost
    /\ inCall' = TRUE
    /\ UNCHANGED <<check, verKnown, apply, src, build, installed, running,
                   status, escrow, persisted>>

Next ==
    \/ CheckStart \/ CheckFinish
    \/ ApplyStart
    \/ PullOk \/ PullFail
    \/ BuildOk \/ BuildFail
    \/ InstallOk \/ InstallFail
    \/ RestartOk \/ RestartFail
    \/ InsertCoin \/ StartCall

Spec == Init /\ [][Next]_vars

-----------------------------------------------------------------------------
(***************************************************************************)
(* Properties.                                                             *)
(***************************************************************************)

TypeOK ==
    /\ check     \in {"idle", "checking", "checked"}
    /\ apply     \in {"idle", "applying"}
    /\ src       \in {"old", "new"}
    /\ build     \in {"old", "wiped", "new"}
    /\ installed \in {"old", "new"}
    /\ running   \in {"old", "new"}
    /\ verKnown  \in BOOLEAN
    /\ inCall    \in BOOLEAN
    /\ escrow    \in Nat
    /\ persisted \in Nat
    /\ budget    \in 0..MaxEvents

(* Holds: nothing is ever installed without having been pulled first -- the
   pipeline's step ordering is genuinely enforced by the status sequencing.

   Note the weaker phrasing. The obvious invariant, "installed = new implies
   build = new", is FALSE and correctly so: a second apply runs `make clean`
   and wipes the build tree while the previously installed binary is still in
   place. That is expected, not a defect. *)
NoInstallWithoutPull == (installed = "new") => (src = "new")

(* Holds: coins are saved after every insert, so a restart restores them. *)
NoCreditLostOnRestart == escrow = persisted

-----------------------------------------------------------------------------
(* VIOLATED -- see UpdaterOpen.cfg.                                        *)

(* A second update check must always be possible unless one is already running.
   This is the property that caught the ratchet: check_state went 0 -> 1 -> 2
   and never back, and updater_check_async required 0 to start, so after the
   first check CheckStart was permanently disabled. Stated with ENABLED because
   the harm is the absence of a possible action, not a bad state.

   NOW HOLDS -- the guard is `check_state != 1`. *)
RecheckAlwaysPossible ==
    (budget > 0 /\ check # "checking") => ENABLED CheckStart

(* "Update applied successfully" must mean the new binary is actually running.
   updater.c:273 discards the systemctl exit code and :276 reports success
   unconditionally. *)
StatusHonest == (status = "success") => (running = installed)

(* A failed apply must not leave the box in a mixed state. `make clean` runs
   before `make daemon` (:250), and the tree is already at the new commit from
   step 1, so a build failure strands new source with no binary and no record
   of the previous commit to go back to. *)
NoStrandedTree == (apply = "idle") => ~(src = "new" /\ build = "wiped")

(* An OTA restart must not drop a call in progress. Nothing in the update path
   consults call state. *)
NoRestartMidCall == ~(status = "restarting" /\ inCall)

=============================================================================
