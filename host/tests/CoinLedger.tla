----------------------------- MODULE CoinLedger -----------------------------
(***************************************************************************)
(* TLA+ model of coin accounting across the daemon's THREE ledgers.        *)
(*                                                                         *)
(* The same balance is stored in three places, mutated by different code   *)
(* paths that do not all notify each other:                                *)
(*                                                                         *)
(*   dcents -- daemon_state->inserted_cents        (daemon_state.h:18)     *)
(*   pcents -- classic_phone_data.inserted_cents   (classic_phone.c:18)    *)
(*   fcents -- persisted_state_t.inserted_cents    (state_persistence.h:8) *)
(*                                                                         *)
(* The only sync channel between the first two is                          *)
(* plugins_adjust_inserted_cents (daemon.c:102-110, added by #109), and it *)
(* is called from exactly two sites in classic_phone.c -- the charge (:396)*)
(* and the refund (:190). Every other write to either ledger is unilateral.*)
(*                                                                         *)
(* SCOPE -- what this deliberately does NOT model:                         *)
(*                                                                         *)
(* Physical coin custody. Conservation against real money would need the   *)
(* meaning of the validator's 'c' / 'z' commands, and docs/COIN_VALIDATOR.md*)
(* documents those with a question mark ("commit/execute (sequence         *)
(* terminator?)") and an explicitly unmitigated reconnect race. Modelling   *)
(* them would mean inventing semantics. The three software ledgers above    *)
(* are fully determined by code, so that is what is checked here.          *)
(***************************************************************************)
EXTENDS Naturals, Sequences

CONSTANTS
    Cost,        \* call.cost_cents (config); 50 in the default config
    CoinValues,  \* denominations the daemon credits (daemon.c:316-322)
    MaxEvents    \* bound on actions, to keep the state space finite

VARIABLES
    state,    \* phone state, restricted to what the coin paths care about
    dcents,   \* the daemon ledger
    pcents,   \* the Classic Phone plugin ledger
    fcents,   \* the persisted file
    budget

vars == <<state, dcents, pcents, fcents, budget>>

Max(a, b) == IF a > b THEN a ELSE b

Init ==
    /\ state  = "IDLE_DOWN"
    /\ dcents = 0
    /\ pcents = 0
    /\ fcents = 0
    /\ budget = MaxEvents

-----------------------------------------------------------------------------
(***************************************************************************)
(* Actions. Each mirrors one code path; the comment gives the site and,     *)
(* crucially, WHICH ledgers that path touches.                              *)
(***************************************************************************)

(* Handset lifted. daemon.c:492 zeroes the daemon ledger; classic_phone.c:142
   zeroes the plugin ledger. BOTH -- consistent. *)
HookUp ==
    /\ budget > 0
    /\ state = "IDLE_DOWN"
    /\ state'  = "IDLE_UP"
    /\ dcents' = 0
    /\ pcents' = 0
    /\ UNCHANGED fcents
    /\ budget' = budget - 1

(* Handset replaced. daemon.c:507 and classic_phone.c:153. BOTH -- consistent. *)
HookDown ==
    /\ budget > 0
    /\ state # "IDLE_DOWN"
    /\ state'  = "IDLE_DOWN"
    /\ dcents' = 0
    /\ pcents' = 0
    /\ UNCHANGED fcents
    /\ budget' = budget - 1

(* A coin drops. daemon.c:328 credits the daemon ledger under the mutex, then
   plugins_handle_coin -> classic_phone.c:63 credits the plugin ledger. Both
   gate on IDLE_UP (daemon.c:213 / classic_phone.c:61). BOTH -- consistent. *)
Coin ==
    /\ budget > 0
    /\ state = "IDLE_UP"
    /\ \E v \in CoinValues :
         /\ dcents' = dcents + v
         /\ pcents' = pcents + v
    /\ UNCHANGED <<state, fcents>>
    /\ budget' = budget - 1

(* Web dashboard "return coins". daemon.c:800-819 zeroes the DAEMON ledger and
   bumps the coin_returns / coins_returned_cents metrics -- and never touches
   the plugin. The plugin still believes the money is there. ONE LEDGER. *)
CoinReturn ==
    /\ budget > 0
    /\ dcents' = 0
    /\ UNCHANGED <<state, pcents, fcents>>
    /\ budget' = budget - 1

(* Classic Phone idle timeout. classic_phone.c:477 zeroes the PLUGIN ledger
   from the tick handler. No event reaches the daemon, so daemon.c never
   learns. ONE LEDGER, the other direction. *)
IdleTimeout ==
    /\ budget > 0
    /\ state = "IDLE_UP"
    /\ pcents' = 0
    /\ UNCHANGED <<state, dcents, fcents>>
    /\ budget' = budget - 1

(* Dialling a paid call. classic_phone.c:395-396 debits the plugin ledger and
   calls plugins_adjust_inserted_cents(-cost) to mirror it. SYNCED -- but note
   the daemon side clamps at zero (daemon.c:107), so if the two ledgers have
   already diverged the clamp silently absorbs the difference instead of
   surfacing it. *)
Charge ==
    /\ budget > 0
    /\ state = "IDLE_UP"
    /\ pcents >= Cost                       \* classic_phone.c:334 gates on the plugin ledger
    /\ pcents' = pcents - Cost
    /\ dcents' = Max(0, dcents - Cost)      \* daemon.c:106-108, clamped
    /\ state'  = "CALL_ACTIVE"
    /\ UNCHANGED fcents
    /\ budget' = budget - 1

(* Call failed while dialling (#91). classic_phone.c:189-190 credits both. *)
Refund ==
    /\ budget > 0
    /\ state = "CALL_ACTIVE"
    /\ pcents' = pcents + Cost
    /\ dcents' = dcents + Cost
    /\ state'  = "IDLE_UP"
    /\ UNCHANGED fcents
    /\ budget' = budget - 1

(* Call ends on the plugin's own timeout: classic_phone_end_call (:430) zeroes
   the PLUGIN ledger. Reached from classic_phone_tick, which raises no daemon
   event, so daemon.c keeps its balance. ONE LEDGER. *)
EndCallTimeout ==
    /\ budget > 0
    /\ state = "CALL_ACTIVE"
    /\ pcents' = 0
    /\ state'  = "IDLE_UP"
    /\ UNCHANGED <<dcents, fcents>>
    /\ budget' = budget - 1

(* daemon_save_state (daemon.c:223-237) snapshots the DAEMON ledger only. *)
Save ==
    /\ budget > 0
    /\ fcents' = dcents
    /\ UNCHANGED <<state, dcents, pcents>>
    /\ budget' = budget - 1

(* Restart + restore, daemon.c:1160-1185.
   Boot order is: plugins_init() -> plugins_activate("Classic Phone") ->
   classic_phone handle_activation (:246) copies daemon_state->inserted_cents
   into the plugin ledger -- and only THEN is the persisted balance written
   into daemon_state (:1171). So the plugin seeds from 0, not from the file.
   daemon.c:1174 re-activates the persisted plugin afterwards, which would
   re-seed it -- but only when the persisted plugin name is non-empty. *)
RestartPluginNameEmpty ==
    /\ budget > 0
    /\ state'  = "IDLE_DOWN"
    /\ dcents' = fcents      \* daemon ledger restored from the file
    /\ pcents' = 0           \* plugin seeded from daemon_state while it was still 0
    /\ UNCHANGED fcents
    /\ budget' = budget - 1

RestartPluginNameSet ==
    /\ budget > 0
    /\ state'  = "IDLE_DOWN"
    /\ dcents' = fcents
    /\ pcents' = fcents      \* re-activation at :1174 re-seeds from the restored value
    /\ UNCHANGED fcents
    /\ budget' = budget - 1

Next ==
    \/ HookUp \/ HookDown \/ Coin
    \/ CoinReturn \/ IdleTimeout
    \/ Charge \/ Refund \/ EndCallTimeout
    \/ Save \/ RestartPluginNameEmpty \/ RestartPluginNameSet

Spec == Init /\ [][Next]_vars

-----------------------------------------------------------------------------
(***************************************************************************)
(* Properties.                                                             *)
(***************************************************************************)

TypeOK ==
    /\ state  \in {"IDLE_DOWN", "IDLE_UP", "CALL_ACTIVE"}
    /\ dcents \in Nat
    /\ pcents \in Nat
    /\ fcents \in Nat
    /\ budget \in 0..MaxEvents

NonNegative == dcents >= 0 /\ pcents >= 0 /\ fcents >= 0

(* THE property. The daemon ledger is what the web API, the metrics gauge and
   the persisted file all report; the plugin ledger is what the customer sees
   on the VFD ("Have: 50c", classic_phone.c:298) and what actually gates
   dialling (:334). If they disagree, the phone is lying to somebody -- either
   the display promises credit the daemon will not honour, or the operator's
   revenue figures do not match what the customer was charged. *)
LedgerAgreement == dcents = pcents

(* Weaker fallback: even if the exact figures drift, the customer should never
   be shown MORE credit than the daemon will actually honour. *)
NeverPromiseMoreThanHeld == pcents <= dcents

(* A restart must not resurrect credit that was already spent or returned, nor
   silently destroy credit the customer still has. *)
PersistenceFaithful == fcents = 0 \/ fcents = dcents

=============================================================================
