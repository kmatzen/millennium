--------------------------- MODULE EventOrdering ---------------------------
(***************************************************************************)
(* TLA+ model of concurrent event ordering in the Millennium daemon.       *)
(*                                                                         *)
(* This discharges the hand-written case analysis in docs/EVENT_ORDERING.md *)
(* (#100), which walks *two* interleavings of *two* events and concludes    *)
(* "idempotent -- redundant but safe". Here we check *all* interleavings of *)
(* every event type, over the two producers that genuinely race.           *)
(*                                                                         *)
(* Relationship to the existing proofs:                                    *)
(*                                                                         *)
(*   tests/cbmc_state_machine.c (make state-check) verifies ONE transition  *)
(*     in isolation, sequentially. It cannot express two events in flight,  *)
(*     so no interleaving property is reachable from it.                    *)
(*   tests/protocol.pml (make model-check) models the wire framing below    *)
(*     this layer -- one opcode, one direction.                             *)
(*                                                                         *)
(* Step() below is deliberately a faithful transcription of the same        *)
(* transitions cbmc_state_machine.c models, so the two stay comparable.     *)
(*                                                                         *)
(* CONCURRENCY MODEL -- why this shape:                                     *)
(*                                                                         *)
(* Handler *bodies* do not run concurrently. daemon.c:1179 holds            *)
(* engine_mutex across the whole engine step, and web control commands go   *)
(* through send_control_command (daemon.c:589) which takes the same mutex.  *)
(* The main loop dequeues exactly ONE event per iteration (daemon.c:1183).  *)
(* So handlers are atomic with respect to each other.                       *)
(*                                                                         *)
(* What is NOT serialized is event *production*. Two independent producers  *)
(* push into one queue (millennium_sdk.c:46):                               *)
(*                                                                         *)
(*   - the serial/Arduino path (hook, coin), read by the main loop          *)
(*   - the PJSUA worker thread (call state), via sip_event_cb               *)
(*     (millennium_sdk.c:169)                                               *)
(*                                                                         *)
(* Their relative order is decided by whichever thread wins g_queue_mutex.  *)
(* That -- not concurrent handler execution -- is the whole race surface.   *)
(*                                                                         *)
(* KEY MODELING DECISION: `hook` is the *physical* handset position, kept   *)
(* separate from `state`, which is only the daemon's *belief*. The daemon's *)
(* 5-state enum conflates handset position with call progress, so the two   *)
(* can silently disagree. Splitting them is what makes the disagreement     *)
(* statable as an invariant.                                                *)
(***************************************************************************)
EXTENDS Naturals, Sequences

CONSTANTS
    MaxEvents,   \* bound on total events produced (keeps the state space finite)
    CoinValues   \* coin denominations in cents; daemon.c:316-322 maps to {5,10,25}

VARIABLES
    hook,     \* physical handset: "up" | "down"  (ground truth)
    dhook,    \* daemon_state->handset_up -- the daemon's *belief* about the
              \* handset, updated only when a hook event is DISPATCHED
    sipcall,  \* the SIP stack's own view of the call (pjsip_interface.c g_call_id)
    state,    \* daemon_state->current_state       (the daemon's belief)
    cents,    \* daemon_state->inserted_cents
    queue,    \* the SDK event queue (FIFO, millennium_sdk.c:46-63)
    budget    \* remaining events a producer may emit

vars == <<hook, dhook, sipcall, state, cents, queue, budget>>

States == {"INVALID", "IDLE_DOWN", "IDLE_UP", "CALL_INCOMING", "CALL_ACTIVE"}

SipEventTypes == {"call_incoming", "call_active", "call_invalid"}

Coin(v) == [type |-> "coin",  val |-> v]
Ev(t)   == [type |-> t,       val |-> 0]

-----------------------------------------------------------------------------
(***************************************************************************)
(* The transition function.                                                *)
(*                                                                         *)
(* Transcribed from daemon.c:                                              *)
(*   coin          -> handle_coin_event        daemon.c:324-345            *)
(*   hook_up/down  -> handle_hook_event        daemon.c:468-514            *)
(*   call_*        -> handle_call_state_event  daemon.c:370-424            *)
(*                                                                         *)
(* Returns <<state', cents'>>.                                             *)
(***************************************************************************)
Step(s, c, h, e) ==
    CASE e.type = "coin" ->
            \* daemon.c:326 gates credit on is_phone_ready_for_operation(),
            \* which is exactly `state == IDLE_UP` (daemon.c:213).
            IF s = "IDLE_UP" THEN <<s, c + e.val, h>> ELSE <<s, c, h>>

      [] e.type = "hook_up" ->
            \* daemon.c records handset_up here, on dispatch.
            IF   s = "CALL_INCOMING" THEN <<"CALL_ACTIVE", c, "up">>   \* answer, daemon.c:476
            ELSE IF s = "IDLE_DOWN"  THEN <<"IDLE_UP", 0, "up">>       \* lift,   daemon.c:485
            ELSE <<s, c, "up">>

      [] e.type = "hook_down" ->
            <<"IDLE_DOWN", 0, "down">>                                 \* daemon.c:497-513

      [] e.type = "call_incoming" ->
            \* #92: accept a ring with the handset either down or up.
            IF s \in {"IDLE_DOWN", "IDLE_UP"} THEN <<"CALL_INCOMING", c, h>> ELSE <<s, c, h>>

      [] e.type = "call_active" ->
            \* daemon.c:387. Guarded on handset_up, not current_state:
            \* CALL_INCOMING is reachable both on-hook and off-hook (#92), so no
            \* current_state test can separate them. This guard is why the daemon
            \* grew a handset_up field -- it was added because of this spec.
            IF h = "up" THEN <<"CALL_ACTIVE", c, h>> ELSE <<s, c, h>>

      [] e.type = "call_invalid" ->
            \* Return to whichever idle state matches the handset: a call ending
            \* while the handset is cradled lands in IDLE_DOWN, not IDLE_UP.
            LET idle == IF h = "up" THEN "IDLE_UP" ELSE "IDLE_DOWN" IN
            IF   s = "CALL_ACTIVE"      THEN <<idle, 0, h>>   \* daemon.c:402, coins cleared
            ELSE IF s = "CALL_INCOMING" THEN <<idle, c, h>>   \* #91: coins PRESERVED, daemon.c:411
            ELSE <<s, c, h>>                                  \* idempotent no-op

      [] OTHER -> <<s, c, h>>

-----------------------------------------------------------------------------

Init ==
    /\ hook    = "down"
    /\ dhook   = "down"
    /\ sipcall = "none"
    /\ state   = "IDLE_DOWN"     \* daemon_state.c:10
    /\ cents   = 0
    /\ queue   = <<>>
    /\ budget  = MaxEvents

(* The user moves the handset. Physical position changes immediately; the
   daemon only learns when the event is dequeued. Firmware emits only on a
   real transition (keypad.ino:164-187), hence d # hook. *)
ProduceHook ==
    /\ budget > 0
    /\ \E d \in {"up", "down"} :
         /\ d # hook
         /\ hook'  = d
         /\ queue' = Append(queue, Ev(IF d = "up" THEN "hook_up" ELSE "hook_down"))
    /\ budget' = budget - 1
    /\ UNCHANGED <<dhook, sipcall, state, cents>>

(* A coin drops. The validator is only armed off-hook (daemon.c:526 sends 'a'
   on the IDLE_UP transition), so physically this needs the handset up. *)
ProduceCoin ==
    /\ budget > 0
    /\ hook = "up"
    /\ \E v \in CoinValues : queue' = Append(queue, Coin(v))
    /\ budget' = budget - 1
    /\ UNCHANGED <<hook, dhook, sipcall, state, cents>>

(* The PJSUA worker thread pushes a call-state event (millennium_sdk.c:169).
   `sipcall` constrains this to a CAUSALLY HONEST SIP stack: it can only report
   ESTABLISHED for a call that was actually ringing, and only report CLOSED for
   a call that exists. Without this, TLC finds a trivial counterexample (a
   spurious CALL_ESTABLISHED out of nowhere) that is fair to dismiss as "the
   SIP stack would never do that". With it, any violation TLC reports is a
   genuine ordering race between two well-behaved producers. *)
ProduceSip ==
    /\ budget > 0
    /\ \E t \in SipEventTypes :
         /\ CASE t = "call_incoming" -> sipcall  = "none"
              [] t = "call_active"   -> sipcall  = "ringing"
              [] OTHER               -> sipcall # "none"
         /\ sipcall' = CASE t = "call_incoming" -> "ringing"
                         [] t = "call_active"   -> "up"
                         [] OTHER               -> "none"
         /\ queue' = Append(queue, Ev(t))
    /\ budget' = budget - 1
    /\ UNCHANGED <<hook, dhook, state, cents>>

(* The main loop: dequeue exactly one event, apply its handler atomically
   (daemon.c:1179-1187, all under engine_mutex). *)
Dispatch ==
    /\ queue # <<>>
    /\ LET e == Head(queue)
           r == Step(state, cents, dhook, e)
       IN  /\ state' = r[1]
           /\ cents' = r[2]
           /\ dhook' = r[3]
    /\ queue' = Tail(queue)
    /\ UNCHANGED <<hook, sipcall, budget>>

Next == ProduceHook \/ ProduceCoin \/ ProduceSip \/ Dispatch

Spec == Init /\ [][Next]_vars /\ WF_vars(Dispatch)

-----------------------------------------------------------------------------
(***************************************************************************)
(* Properties.                                                             *)
(***************************************************************************)

TypeOK ==
    /\ hook    \in {"up", "down"}
    /\ dhook   \in {"up", "down"}
    /\ sipcall \in {"none", "ringing", "up"}
    /\ state   \in States
    /\ cents   \in Nat
    /\ budget  \in 0..MaxEvents

(* Already proven by CBMC for a single step; restated here to confirm they
   survive arbitrary interleaving. Expected to HOLD. *)
NoInvalidState == state # "INVALID"

(* SUBSUMED by TypeOK (`cents \in Nat`), so it can never be the invariant TLC
   reports -- any state violating this violates TypeOK too. Kept because it
   states the intent in the reader's terms, but it adds no coverage: do not
   count it when judging how much this spec actually checks.
   Confirmed by ./mutation_audit.sh, where the underflow mutation is caught by
   TypeOK rather than by this. *)
NonNegative    == cents >= 0

(* Credit must never survive the handset going down (the coins were either
   collected or returned by the 'c','z' pair at daemon.c:530-531). *)
DownImpliesNoCredit == (state = "IDLE_DOWN") => (cents = 0)

Quiesced == queue = <<>> /\ budget = 0

(* EVENT_ORDERING.md's claim, formalized: once every queued event has been
   processed, the daemon's belief agrees with physical reality. The doc argues
   this by checking two orderings by hand; this checks all of them.

   CALL_INCOMING is an accepted resting state with the handset down -- that is
   just a phone ringing in its cradle, which #92 explicitly supports. What must
   NOT survive is IDLE_UP or CALL_ACTIVE. *)
Converged ==
    Quiesced => (hook = "down" =>
                    /\ state \in {"IDLE_DOWN", "CALL_INCOMING"}
                    /\ cents = 0)

(* The specific unrecoverable case the daemon.c:387 guard was added to fix: a
   CALL_ESTABLISHED dispatched after the user already hung up used to leave the
   phone parked in CALL_ACTIVE with the handset in the cradle, with nothing left
   in the queue to recover it. Distinct from Converged, which also constrains
   IDLE_UP and credit. *)
SettledNotActiveOnHook == Quiesced => ~(hook = "down" /\ state = "CALL_ACTIVE")

(* STRONGER, and still violated -- see EventOrderingRace.cfg.

   The daemon should never believe a call is up while the handset is cradled:
   audio plays to nobody and the far end is connected to a phone no one
   answered. The guard at daemon.c:387 cannot fully establish this, because the
   daemon's five-state enum CONFLATES ringing-on-hook with ringing-off-hook --
   both are CALL_INCOMING (#92). So a SIP-side answer during an on-hook ring
   still lands in CALL_ACTIVE with the handset down.

   Unlike the case above this one is recoverable (hook_down settles it, and
   hook_up is correctly a no-op from CALL_ACTIVE), so it is a phantom-answer
   defect rather than a stuck state. Closing it properly needs the handset
   position tracked as its own field in daemon_state_data_t rather than
   inferred from current_state. *)
ActiveImpliesOffHook == (state = "CALL_ACTIVE") => (hook = "up")

(* NOTE deliberately NOT asserted as an invariant:
     (state = "IDLE_UP") => (hook = "up")
   This is violated *transiently* and that is by design -- it is exactly the
   window EVENT_ORDERING.md calls "Order 2": CALL_INVALID lands first and moves
   the daemon to IDLE_UP, then the queued hook_down arrives and settles it to
   IDLE_DOWN. Converged is the right way to state that claim, because it only
   constrains the settled state. *)

=============================================================================
