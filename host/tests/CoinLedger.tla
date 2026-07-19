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
(* RESOLVED (#222). There is now ONE ledger. classic_phone, jukebox and       *)
(* fortune_teller no longer keep a private inserted_cents; they read and      *)
(* write daemon_state->inserted_cents through sdk_balance/sdk_spend_balance/  *)
(* sdk_add_balance/sdk_clear_balance, which is the pattern number_guess (the  *)
(* canonical example) already used.                                          *)
(*                                                                           *)
(* This spec is kept as the regression guard. pcents is retained as a         *)
(* separate variable ON PURPOSE: every action now keeps it equal to dcents by *)
(* construction, so LedgerAgreement holding is a statement that no code path  *)
(* updates one without the other. Re-introducing a private ledger means       *)
(* re-introducing an unsynced action here, and the invariant fails again.     *)
(*                                                                           *)
(* Previously the only sync channel was plugins_adjust_inserted_cents         *)
(* (#109), called from just two sites, and four other paths wrote one ledger  *)
(* without the other -- coin return, the plugin idle timeout, the plugin call *)
(* timeout, and the boot ordering. All four are modelled below as they now    *)
(* behave.                                                                    *)
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
    /\ fcents' = 0
    /\ budget' = budget - 1

(* Handset replaced. daemon.c:507 and classic_phone.c:153. BOTH -- consistent. *)
HookDown ==
    /\ budget > 0
    /\ state # "IDLE_DOWN"
    /\ state'  = "IDLE_DOWN"
    /\ dcents' = 0
    /\ pcents' = 0
    /\ fcents' = 0
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
         /\ fcents' = dcents + v            \* handle_coin_event saves
    /\ UNCHANGED state
    /\ budget' = budget - 1

(* Web dashboard "return coins". daemon.c:800-819 zeroes the DAEMON ledger and
   bumps the coin_returns / coins_returned_cents metrics -- and never touches
   the plugin. The plugin still believes the money is there. ONE LEDGER. *)
CoinReturn ==
    /\ budget > 0
    /\ dcents' = 0
    /\ pcents' = 0          \* one ledger: the plugin reads sdk_balance() live
    /\ fcents' = 0
    /\ UNCHANGED state
    /\ budget' = budget - 1

(* Classic Phone idle timeout. classic_phone.c:477 zeroes the PLUGIN ledger
   from the tick handler. No event reaches the daemon, so daemon.c never
   learns. ONE LEDGER, the other direction. *)
IdleTimeout ==
    /\ budget > 0
    /\ state = "IDLE_UP"
    /\ pcents' = 0
    /\ dcents' = 0          \* classic_phone.c now calls sdk_clear_balance()
    /\ fcents' = 0          \* tick -> keypad-less path; persisted on the next event
    /\ UNCHANGED state
    /\ budget' = budget - 1

(* Dialling a paid call: sdk_spend_balance(cost). Note the clamp at
   daemon.c:107 is still there; with a single ledger it can no longer absorb a
   divergence, because there is none to absorb. *)
Charge ==
    /\ budget > 0
    /\ state = "IDLE_UP"
    /\ pcents >= Cost                       \* classic_phone.c:334 gates on the plugin ledger
    /\ pcents' = pcents - Cost
    /\ dcents' = Max(0, dcents - Cost)      \* daemon.c:106-108, clamped
    /\ fcents' = Max(0, dcents - Cost)      \* handle_keypad_event now saves
    /\ state'  = "CALL_ACTIVE"
    /\ budget' = budget - 1

(* Call failed while dialling (#91). classic_phone.c:189-190 credits both. *)
Refund ==
    /\ budget > 0
    /\ state = "CALL_ACTIVE"
    /\ pcents' = pcents + Cost
    /\ dcents' = dcents + Cost
    /\ fcents' = dcents + Cost              \* refund runs from a call-state event, which saves
    /\ state'  = "IDLE_UP"
    /\ budget' = budget - 1

(* Call ends on the plugin's own timeout: classic_phone_end_call (:430) zeroes
   the PLUGIN ledger. Reached from classic_phone_tick, which raises no daemon
   event, so daemon.c keeps its balance. ONE LEDGER. *)
EndCallTimeout ==
    /\ budget > 0
    /\ state = "CALL_ACTIVE"
    /\ pcents' = 0
    /\ dcents' = 0          \* classic_phone_end_call now calls sdk_clear_balance()
    /\ fcents' = 0
    /\ state'  = "IDLE_UP"
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
(* The boot-ordering path is gone too: no plugin seeds a balance on activation
   any more, so there is nothing to seed early. Both restart cases collapse to
   the same behaviour -- the plugin reads whatever the daemon restored. *)
RestartPluginNameEmpty ==
    /\ budget > 0
    /\ state'  = "IDLE_DOWN"
    /\ dcents' = fcents
    /\ pcents' = fcents
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
   silently destroy credit the customer still has.
   
   Stated as "the file always matches the live balance". The earlier form
   (fcents = 0 \/ fcents = dcents) was wrong and never actually evaluated --
   it sat in the drift config behind LedgerAgreement, which failed first.
   
   This is what caught the real bug: handle_keypad_event was the only event
   handler that never called daemon_save_state, and a keypress is how money
   gets SPENT. Insert 50c (saved), dial (charged, live 0, file still 50),
   restart -- and the customer got their money back. *)
PersistenceFaithful == fcents = dcents

=============================================================================
