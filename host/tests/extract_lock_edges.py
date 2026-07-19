#!/usr/bin/env python3
"""Extract the daemon's mutex acquisition graph from the source (#231).

Walks every host/*.c file tracking pthread_mutex_lock/unlock nesting, takes the
transitive closure over the call graph, and prints every (outer, inner) pair
where one mutex is acquired while another is held -- then checks for cycles.

    python3 tests/extract_lock_edges.py

This is what the chains in tests/LockOrder.tla were derived from. Re-run it
after touching locking; if the edge set changed, update LockOrder.tla's chains
and the comment in daemon.c.

LIMITATION, and it matters: this cannot see through function pointers. Two
edges in this codebase run through callbacks and must be found by reading:

  g_monitor_mutex -> daemon_state_mutex
      health_monitor_run_all_checks holds g_monitor_mutex across execute_check,
      which dispatches check->check_function, registered in daemon.c as
      check_daemon_activity, which locks daemon_state_mutex.

  plugins_mutex -> (plugin handler)
      currently NO edge, because #226 and #223 moved every plugin callback
      outside the lock. That is load-bearing: if a callback is ever invoked
      under plugins_mutex again, this script will not tell you.

So "no cycle" here is necessary, not sufficient. LockOrder.tla adds the
callback edge by hand and model-checks the threads running concurrently.
"""
import re, glob, collections

lock_re   = re.compile(r'pthread_mutex_lock\(\s*\(?[^)]*?&?([A-Za-z_][A-Za-z0-9_>\-\.\[\]]*?)\s*\)')
unlock_re = re.compile(r'pthread_mutex_unlock\(\s*\(?[^)]*?&?([A-Za-z_][A-Za-z0-9_>\-\.\[\]]*?)\s*\)')
call_re   = re.compile(r'\b([a-z_][a-z0-9_]*)\s*\(')
func_re   = re.compile(r'^[A-Za-z_].*?\b([a-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*\{')
KW = {'if','while','for','switch','sizeof','return','pthread_mutex_lock','pthread_mutex_unlock','memset','memcpy','snprintf','strlen','strcmp','free','malloc'}

direct = collections.defaultdict(set)      # fn -> mutexes locked directly
calls  = collections.defaultdict(set)      # fn -> callees (any)
held_calls = []                            # (fn, held_tuple, callee, file, line)

for path in sorted(glob.glob('*.c')):
    lines = open(path, errors='replace').read().split('\n')
    cur, depth, held = None, 0, []
    for i,l in enumerate(lines,1):
        m = func_re.match(l)
        if m and depth==0: cur=m.group(1); held=[]
        for mm in lock_re.finditer(l):
            n=mm.group(1).strip()
            if cur: direct[cur].add(n)
            held.append(n)
        for mm in unlock_re.finditer(l):
            n=mm.group(1).strip()
            if n in held: held.remove(n)
        if cur:
            for c in call_re.finditer(l):
                fn=c.group(1)
                if fn in KW: continue
                calls[cur].add(fn)
                if held: held_calls.append((cur, tuple(held), fn, path, i))
        depth += l.count('{')-l.count('}')
        if depth<=0: cur=None; held=[]

# transitive closure: mutexes reachable from a function
reach = {f:set(s) for f,s in direct.items()}
for f in calls:
    reach.setdefault(f,set())
changed=True
while changed:
    changed=False
    for f, cs in calls.items():
        cur=reach.setdefault(f,set())
        for c in cs:
            add=reach.get(c,set())
            if not add.issubset(cur):
                cur|=add; changed=True

edges=collections.defaultdict(set)
for fn, held, callee, path, line in held_calls:
    for inner in reach.get(callee,set()):
        for outer in held:
            if outer!=inner:
                edges[outer].add((inner, callee, f"{path}:{line}"))

print("=== LOCK-ORDER EDGES (outer -> inner), transitive through calls ===")
for o in sorted(edges):
    for (i2,via,loc) in sorted(edges[o]):
        print(f"  {o:22s} -> {i2:22s}  via {via}()  {loc}")

# cycle detection
g={o:{i for (i,_,_) in v} for o,v in edges.items()}
def find_cycle(g):
    WHITE,GREY,BLACK=0,1,2
    color=collections.defaultdict(int); stack=[]; out=[]
    def dfs(u):
        color[u]=GREY; stack.append(u)
        for v in g.get(u,()):
            if color[v]==GREY:
                out.append(stack[stack.index(v):]+[v]); return True
            if color[v]==WHITE and dfs(v): return True
        color[u]=BLACK; stack.pop(); return False
    for n in list(g):
        if color[n]==WHITE and dfs(n): break
    return out
c=find_cycle(g)
print()
print("=== CYCLE CHECK ===")
print("CYCLE FOUND:", c if c else "none — acquisition graph is acyclic")
