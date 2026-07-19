----------------------------- MODULE LockOrder -----------------------------
(***************************************************************************)
(* TLA+ model of the daemon's mutex acquisition graph: deadlock-freedom.   *)
(*                                                                         *)
(* Follow-up to #231, which documented the lock order. Documenting an order *)
(* is not the same as proving nothing violates it, and a counterexample here *)
(* is a hang on real hardware rather than a wrong number.                   *)
(*                                                                         *)
(* HOW THE CHAINS WERE DERIVED                                             *)
(*                                                                         *)
(* Not from the comment in daemon.c -- from the source. A script walked     *)
(* every host/*.c file tracking pthread_mutex_lock/unlock nesting, took the *)
(* transitive closure over the call graph, and emitted every (outer, inner) *)
(* pair where one mutex is acquired while another is held. That found 15    *)
(* edges, including two the #231 comment had missed:                        *)
(*                                                                         *)
(*   daemon_state_mutex -> plugins_mutex                                    *)
(*     daemon_broadcast_state holds daemon_state_mutex and calls            *)
(*     plugins_get_active_name (daemon.c:304-306).                          *)
(*   daemon_state_mutex -> logger_mutex / log_queue.lock                    *)
(*     the event handlers log from inside the state-mutex region.           *)
(*                                                                         *)
(* Static analysis CANNOT see through function pointers, so one edge was    *)
(* added by hand after verifying it by reading:                             *)
(*                                                                         *)
(*   g_monitor_mutex -> daemon_state_mutex                                  *)
(*     health_monitor_run_all_checks holds g_monitor_mutex across           *)
(*     execute_check (health_monitor.c:213-216), which dispatches through   *)
(*     check->check_function (:397) -- a callback registered in daemon.c    *)
(*     (:1138) as check_daemon_activity, which locks daemon_state_mutex     *)
(*     (:948). No call-graph tool follows that; it has to be read.          *)
(*                                                                         *)
(* Plugin handlers are function pointers too, but since #226 and #223 they  *)
(* are all invoked OUTSIDE plugins_mutex, so they contribute no edge. That  *)
(* is load-bearing: reverting either change would add                       *)
(* plugins_mutex -> daemon_state_mutex and close a cycle. The Mutated       *)
(* config below demonstrates exactly that.                                  *)
(*                                                                         *)
(* WHAT THIS ADDS OVER THE STATIC ANALYSIS                                  *)
(*                                                                         *)
(* The script can only say "the graph has no cycle". This runs the threads  *)
(* concurrently and asks whether a state exists in which every thread is    *)
(* blocked -- which is what actually matters, and which also catches        *)
(* deadlocks a naive edge-cycle check would miss (e.g. a thread that holds  *)
(* two locks acquired in separate, individually-consistent chains).         *)
(*                                                                         *)
(* Chains are deliberately OVER-approximated: each thread is modelled as    *)
(* holding the maximal nest it could hold, even where the real code takes   *)
(* and releases a lock before the next. Over-approximating is the safe      *)
(* direction -- it can report a deadlock that cannot happen, never miss one *)
(* that can.                                                                *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets

(* Set TRUE to reverse one edge and confirm this model can actually detect a
   deadlock -- see MutatedChains below. *)
CONSTANT Mutated

NoThread == "-"

(***************************************************************************)
(* The real acquisition chains, derived from the source (see header).       *)
(*                                                                          *)
(* Defined here rather than in the .cfg because TLC's config parser will not *)
(* take a nested record-of-sequences literal.                                *)
(***************************************************************************)
RealChains ==
    [ MainLoop     |-> << <<"engine","g_monitor","daemon_state","plugins">>,
                          <<"engine","daemon_state","metrics">>,
                          <<"engine","daemon_state","logger">>,
                          <<"engine","g_queue">>,
                          <<"engine","tone">> >>,
      WebWorker    |-> << <<"engine","daemon_state","plugins">>,
                          <<"engine","daemon_state","metrics">>,
                          <<"web_state">>,
                          <<"metrics">> >>,
      HealthThread |-> << <<"g_monitor","daemon_state","metrics">>,
                          <<"g_monitor","daemon_state","logger">> >>,
      PjsuaWorker  |-> << <<"g_queue">>,
                          <<"g_sip">>,
                          <<"logger">> >>,
      LoggerWriter |-> << <<"logger_file","log_queue">>,
                          <<"log_queue">> >> ]

(***************************************************************************)
(* The same system with ONE edge reversed: a plugin callback that takes     *)
(* daemon_state_mutex while plugins_mutex is held. That is exactly what     *)
(* plugins_activate did before #226 (handle_activation ran under the lock)  *)
(* and what plugins_activate would do again if sdk_release_session were     *)
(* moved inside the lock.                                                   *)
(*                                                                          *)
(* This exists to validate the model. A deadlock-freedom result is only     *)
(* meaningful if the same model reports a deadlock when one is present.     *)
(***************************************************************************)
MutatedChains ==
    [ RealChains EXCEPT
        !.WebWorker = Append(@, <<"plugins","daemon_state">>) ]

Chains == IF Mutated THEN MutatedChains ELSE RealChains

Threads == DOMAIN Chains
Locks   == UNION { UNION { {Chains[t][c][i] : i \in DOMAIN Chains[t][c]}
                           : c \in DOMAIN Chains[t] }
                   : t \in Threads }

VARIABLES
    owner,   \* Lock -> Thread holding it, or NoThread
    pos,     \* Thread -> how many locks of its current chain it holds
    pick     \* Thread -> index of the chain it is currently walking

vars == <<owner, pos, pick>>

Init ==
    /\ owner = [l \in Locks |-> NoThread]
    /\ pos   = [t \in Threads |-> 0]
    /\ pick  = [t \in Threads |-> 1]

Chain(t)   == Chains[t][pick[t]]
NextLock(t) == Chain(t)[pos[t] + 1]

(* A thread at rest picks which code path it is about to run. *)
Choose(t) ==
    /\ pos[t] = 0
    /\ \E c \in DOMAIN Chains[t] : pick' = [pick EXCEPT ![t] = c]
    /\ UNCHANGED <<owner, pos>>

(* Acquire the next lock in the chain -- blocks (action disabled) if another
   thread holds it. This is a plain blocking mutex: no trylock, no timeout,
   which is what makes a cycle fatal. *)
Acquire(t) ==
    /\ pos[t] < Len(Chain(t))
    /\ owner[NextLock(t)] = NoThread
    /\ owner' = [owner EXCEPT ![NextLock(t)] = t]
    /\ pos'   = [pos EXCEPT ![t] = pos[t] + 1]
    /\ UNCHANGED pick

(* Finished the critical section: drop everything and go idle. Modelling the
   release as all-at-once is fine -- holding locks for longer than the real
   code does is the conservative direction. *)
Release(t) ==
    /\ pos[t] = Len(Chain(t))
    /\ pos[t] > 0
    /\ owner' = [l \in Locks |->
                    IF owner[l] = t THEN NoThread ELSE owner[l]]
    /\ pos'   = [pos EXCEPT ![t] = 0]
    /\ UNCHANGED pick

Next == \E t \in Threads : Choose(t) \/ Acquire(t) \/ Release(t)

Spec == Init /\ [][Next]_vars

-----------------------------------------------------------------------------

TypeOK ==
    /\ owner \in [Locks -> Threads \cup {NoThread}]
    /\ \A t \in Threads : pos[t] \in 0..Len(Chain(t))

(* A lock is held by at most one thread, and a thread's held count matches
   what it actually owns. Sanity on the model itself. *)
HeldConsistent ==
    \A t \in Threads :
        Cardinality({l \in Locks : owner[l] = t}) = pos[t]

(***************************************************************************)
(* DEADLOCK-FREEDOM.                                                        *)
(*                                                                          *)
(* NOT checked via TLC's built-in deadlock detection. That flags only a     *)
(* state with NO successor at all -- a global stall -- and a lock-order bug *)
(* does not produce one: two threads can be locked in a circular wait       *)
(* forever while the other three keep running happily. The first version of *)
(* this spec relied on the built-in check, and it reported "no error" even  *)
(* on the deliberately-cycled MutatedChains. A deadlock-freedom result is   *)
(* worthless unless the same model catches a planted deadlock, which is the *)
(* entire reason MutatedChains exists.                                      *)
(*                                                                          *)
(* So circular wait is stated directly: a thread is Blocked when the next   *)
(* lock in its chain is held by someone else, and a deadlock is any         *)
(* non-empty set of threads in which every member is blocked waiting on a   *)
(* lock held by another member -- the standard waits-for cycle.             *)
(***************************************************************************)

Blocked(t) ==
    /\ pos[t] < Len(Chain(t))
    /\ owner[NextLock(t)] # NoThread
    /\ owner[NextLock(t)] # t

WaitsFor(t) == owner[NextLock(t)]

NoCircularWait ==
    ~ \E S \in SUBSET Threads :
        /\ S # {}
        /\ \A t \in S : Blocked(t) /\ WaitsFor(t) \in S

=============================================================================
