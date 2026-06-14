/*
 * SPIN model of "The Operator: The Last Call" game state machine
 * (plugins/time_operator.c). The three number-pieces are abstracted to a counter
 * and the tick-driven beats (travel/reveal/flavor/final) to immediate steps;
 * the branching player choices are nondeterministic.
 *
 * Properties (make game-check):
 *   - no dead-ends   : no invalid end states (every state but DORMANT, the
 *                      on-hook idle, always has a move).
 *   - bounded pieces : ltl pieces_ok { [] (pieces <= 3) }.
 *   - no soft-lock   : ltl can_win { <> everWon } under weak fairness. The player
 *     acts arbitrarily until a separate one-shot process (committer) sets
 *     `committed` — weak fairness guarantees that process eventually runs (it's a
 *     continuously-enabled process; SPIN fairness is process-level). Once
 *     committed, only progress moves are enabled, so the game marches to WIN.
 *     Thus can_win holds iff WIN is reachable from EVERY reachable state (the
 *     commit can land anywhere) — i.e. AG EF win, no trap/soft-lock.
 */

#define DORMANT 0
#define HUB     1
#define KEYERA  2
#define REVEAL  3
#define FLAVOR  4
#define READY   5
#define FINAL   6
#define WIN     7

byte st = DORMANT;
byte pieces = 0;
bool pass = false;       /* temporal pass (card) or coin-forced sealed era */
bool everWon = false;
bool committed = false;  /* has the player committed to cooperative play? */

/* One-shot: weak fairness guarantees this continuously-enabled process runs, so
 * `committed` becomes true at some (nondeterministic) point in any run. */
active proctype committer() { committed = true }

active proctype game() {
end_dormant:
    do
    :: st == DORMANT ->
        if :: committed -> skip :: else -> if :: pieces = 0; pass = false :: skip fi fi;
        if :: (pieces == 3) -> st = READY :: else -> st = HUB fi
    :: st == HUB ->
        if
        :: st = KEYERA                  /* dial the current target key year */
        :: !committed -> st = FLAVOR    /* wrong / flavor year */
        :: !committed -> st = DORMANT   /* hang up */
        fi
    :: st == FLAVOR -> st = HUB
    :: st == KEYERA ->
        if
        :: (pieces == 2 && !pass) ->
            if :: pass = true :: !committed -> st = HUB fi   /* open it, or give up */
        :: else -> skip
        fi;
        if
        :: (st == KEYERA) ->
            if
            :: pieces = pieces + 1; st = REVEAL   /* LISTEN -> piece */
            :: !committed -> st = KEYERA          /* SPEAK -> tangle (recoverable) */
            :: !committed -> st = HUB             /* # / drift back */
            fi
        :: else -> skip
        fi
    :: st == REVEAL ->
        if :: (pieces == 3) -> st = READY :: else -> st = HUB fi
    :: st == READY ->
        if
        :: st = FINAL                   /* dial the full number correctly */
        :: !committed -> st = READY     /* wrong number, retry */
        :: !committed -> st = DORMANT   /* hang up */
        fi
    :: st == FINAL -> st = WIN; everWon = true
    :: st == WIN -> st = DORMANT; pieces = 0; pass = false   /* hang up resets */
    od
}

ltl pieces_ok { [] (pieces <= 3) }
ltl can_win   { <> everWon }
