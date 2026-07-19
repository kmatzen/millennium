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
    callDropped,   \* ghost: TRUE if an update restart ever cut off a live call
    badInstall,    \* ghost: TRUE if anything was ever installed without a pull
    budget

vars == <<check, verKnown, apply, src, build, installed, running,
          status, inCall, escrow, persisted, callDropped, badInstall, budget>>

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
    /\ callDropped = FALSE
    /\ badInstall  = FALSE
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
                   status, inCall, escrow, persisted, callDropped, badInstall>>

CheckFinish ==
    /\ Tick
    /\ check = "checking"
    /\ check' = "checked"
    /\ \E ok \in {TRUE, FALSE} : verKnown' = ok   \* curl may fail (:44, :51, :57)
    /\ UNCHANGED <<apply, src, build, installed, running,
                   status, inCall, escrow, persisted, callDropped, badInstall>>

-----------------------------------------------------------------------------
(***************************************************************************)
(* The apply pipeline -- updater_apply, updater.c:226-278. Four ordered     *)
(* shell commands, each of which can fail. There is no rollback at any      *)
(* step, and the return code of the LAST one is never checked (:273).       *)
(***************************************************************************)

ApplyStart ==
    /\ Tick
    /\ apply = "idle"
    /\ ~inCall                    \* #225: /api/update returns 409 during a call
    /\ apply'  = "applying"
    /\ status' = "pulling"
    /\ UNCHANGED <<check, verKnown, src, build, installed, running,
                   inCall, escrow, persisted, callDropped, badInstall>>

(* Step 1: git pull --ff-only. On failure the pipeline returns -1 with the
   tree untouched -- this step alone is clean. *)
PullOk ==
    /\ Tick
    /\ apply = "applying" /\ status = "pulling"
    /\ src' = "new"
    /\ status' = "building"
    /\ UNCHANGED <<check, verKnown, apply, build, installed, running,
                   inCall, escrow, persisted, callDropped, badInstall>>

PullFail ==
    /\ Tick
    /\ apply = "applying" /\ status = "pulling"
    /\ apply'  = "idle"
    /\ status' = "err_pull"
    /\ UNCHANGED <<check, verKnown, src, build, installed, running,
                   inCall, escrow, persisted, callDropped, badInstall>>

(* Step 2: `make clean && make daemon`. make clean runs FIRST and
   unconditionally, so a build failure leaves no binary behind -- and the tree
   is already at the new commit from step 1. *)
BuildOk ==
    /\ Tick
    /\ apply = "applying" /\ status = "building"
    /\ build'  = "new"
    /\ status' = "installing"
    /\ UNCHANGED <<check, verKnown, apply, src, installed, running,
                   inCall, escrow, persisted, callDropped, badInstall>>

BuildFail ==
    /\ Tick
    /\ apply = "applying" /\ status = "building"
    /\ build'  = "wiped"          \* make clean already ran; make daemon did not finish
    /\ src'    = "old"            \* #224: rollback_to(prev_commit)
    /\ apply'  = "idle"
    /\ status' = "err_build"
    /\ UNCHANGED <<check, verKnown, installed, running,
                   inCall, escrow, persisted, callDropped, badInstall>>

(* Step 3: sudo make install. *)
InstallOk ==
    /\ Tick
    /\ apply = "applying" /\ status = "installing"
    /\ badInstall' = (badInstall \/ (src # "new"))
    /\ installed' = "new"
    /\ status'    = "restarting"
    /\ UNCHANGED <<check, verKnown, apply, src, build, running,
                   inCall, escrow, persisted, callDropped, badInstall>>

InstallFail ==
    /\ Tick
    /\ apply = "applying" /\ status = "installing"
    /\ src'    = "old"            \* #224: source rolled back...
    /\ apply'  = "idle"
    /\ status' = "err_install"
    \* ...but `installed` is deliberately NOT restored: undoing an install
    \* needs a backup of the previous binary, which does not exist. The status
    \* string says so rather than pretending otherwise.
    /\ UNCHANGED <<check, verKnown, build, installed, running,
                   inCall, escrow, persisted, callDropped, badInstall>>

(* Step 4: sudo systemctl restart daemon.service.
   updater.c:273 calls run_command WITHOUT checking its return value, then
   unconditionally sets apply_status to "Update applied successfully" (:276).
   Modelled faithfully: both outcomes lead to the same reported status.

   The restart is also not coordinated with call state -- nothing in
   web_server_handle_api_update consults web_server_is_in_call() or
   daemon_state->current_state. *)
(* #225 second check: the guard re-tests call state immediately before the
   restart, because the build has taken minutes since /api/update refused.
   Deferring rather than dropping the call -- the installed binary takes effect
   at the next restart, whenever that is. *)
RestartDeferred ==
    /\ Tick
    /\ apply = "applying" /\ status = "restarting"
    /\ inCall
    /\ apply'  = "idle"
    /\ status' = "deferred"
    /\ UNCHANGED <<check, verKnown, src, build, installed, running,
                   inCall, escrow, persisted, callDropped, badInstall>>

RestartOk ==
    /\ Tick
    /\ apply = "applying" /\ status = "restarting"
    /\ ~inCall
    \* Records whether this restart cut off a live call. With the ~inCall guard
    \* above it cannot, which is exactly what NoCallDroppedByUpdate asserts --
    \* remove the guard (the pre-#225 code) and this latches TRUE.
    /\ callDropped' = (callDropped \/ inCall)
    /\ running'   = installed
    /\ inCall'    = FALSE            \* the process dies; any call goes with it
    /\ escrow'    = persisted        \* restored from the file at daemon.c:1171
    /\ apply'     = "idle"
    /\ status'    = "success"
    /\ UNCHANGED <<check, verKnown, src, build, installed, persisted, badInstall>>

RestartFail ==
    /\ Tick
    /\ apply = "applying" /\ status = "restarting"
    /\ ~inCall
    /\ apply'  = "idle"
    /\ status' = "err_restart"       \* now checked -- updater.c:273
    /\ UNCHANGED <<check, verKnown, src, build, installed, running,
                   inCall, escrow, persisted, callDropped, badInstall>>

-----------------------------------------------------------------------------
(* Ordinary phone activity, so the restart has something to interrupt. *)

InsertCoin ==
    /\ Tick
    /\ ~inCall
    /\ escrow' = escrow + CallCost
    /\ persisted' = escrow + CallCost      \* daemon_save_state after each coin
    /\ UNCHANGED <<check, verKnown, apply, src, build, installed, running,
                   status, inCall, callDropped, badInstall>>

StartCall ==
    /\ Tick
    /\ ~inCall /\ escrow >= CallCost
    /\ inCall' = TRUE
    /\ UNCHANGED <<check, verKnown, apply, src, build, installed, running,
                   status, escrow, persisted, callDropped, badInstall>>

Next ==
    \/ CheckStart \/ CheckFinish
    \/ ApplyStart
    \/ PullOk \/ PullFail
    \/ BuildOk \/ BuildFail
    \/ InstallOk \/ InstallFail
    \/ RestartOk \/ RestartFail \/ RestartDeferred
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

(* Nothing is ever installed without having been pulled first.

   Stated over a ghost flag, because the state-predicate forms are both false
   for legitimate reasons:
     "installed = new => build = new" -- a SECOND apply runs `make clean` and
       wipes the build tree while the previously installed binary is in place.
     "installed = new => src = new"   -- since #224 a failed apply rolls the
       source back while the installed binary is deliberately left alone.
   Neither is a defect; both are the system working as designed. What must
   never happen is an install step running without its pull, and that is what
   this records. *)
NoInstallWithoutPull == ~badInstall

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
   before `make daemon` and the tree is already at the new commit, so a build
   failure used to strand new source with no binary and no record of where it
   came from. Holds since #224: the pre-update commit is captured with
   `git rev-parse HEAD` before the pull, and a failure at build or install
   resets the tree back to it. *)
NoStrandedTree == (apply = "idle") => ~(src = "new" /\ build = "wiped")

(* An OTA restart must not drop a call in progress. Holds since #225: the
   handler checks web_server_is_in_call() and refuses with 409, so an apply
   cannot even start during a call. Refusing rather than deferring -- queueing
   would need somewhere to park the request and a way to report it pending. *)
(* Stated over a ghost flag rather than a state predicate. "status = restarting
   implies not inCall" is too strong -- the phone can legitimately be picked up
   while an apply sits at that step; what must not happen is the RESTART firing
   anyway. And "running = new implies not inCall" is nonsense, since calls
   happen normally after an update. So: did any restart ever cut off a call. *)
NoCallDroppedByUpdate == ~callDropped

=============================================================================
