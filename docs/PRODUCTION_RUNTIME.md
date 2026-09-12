# Shared production runtime

KKnD, Dark Reign and 7th Legion use the same level-owned production queues from
the interactive driver and model API. `G_QueueProduct` validates the actual
producer, prerequisites, queue capacity and its owner's budget. The human
command entry points restrict producers to owner zero. `G_ProductionTicker`
walks the global thinker ring and creates ordinary mobjs; model object lists
remain borrowed views refreshed after spawning. Queue completion reloads the
queue pointer after releasing storage, avoiding a read after the final item.

AI goals count living actors plus queued products for each non-player owner.
Completed goals stop spending, later goals can progress, and lost units are
replaced. These are authored engine skirmish goals, not native AI scripts.
The interactive driver also ticks the existing combat AI. A shared sidebar
lists products for the selected human producer, with costs, queue counts and
pagination; clicking queues on that specific object. This is a basic engine
interface. Dark Colony continues to use its existing custom interface path.

Dark Reign's native product numbers must not index `mobjinfo[]`. Actor stats
and producers now use dense `MT_*` indices, matching Doom's mobj type contract.
Product and prerequisite numbers retain their native identity and resolve via
`mobjinfo[].doomednum`, as Doom resolves map thing numbers before spawning.
Previously, for example, native HQ number 10001 was used as a runtime state
table index, while infantry could enter an unrelated actor's states. This is
a runtime table correction; no new native balance or executable claim is made.

Regression tests in each game's `test_production.c` exercise two enemy owners,
budget isolation, bounded goals and replacement, a sidebar click with no model
object, selected-producer identity, queue limits and completion. The 7th Legion
combat test checks each armed type through the normal ticker. The KKnD catalog
test checks runtime texture loading and every state's native frame bounds.

Run `env SDL_VIDEODRIVER=dummy make test` for the complete suite.
