# random

Random numbers and selections, as Python's `random` module. The generator is Python's Mersenne Twister with Python's seeding, so after `random-seed N` each command returns what the same Python call returns after `random.seed(N)`.

| Command | Result |
|-|-|
| `random-seed N` | Seed the generator with the integer N, within 2^53. No value. |
| `random` | A number in [0, 1), as `random.random()`. |
| `random-int A B` | An integer from A to B inclusive, as `random.randint`. |
| `random-uniform A B` | A number between A and B, as `random.uniform`. |
| `random-gauss MU SIGMA` | A normally distributed number, as `random.gauss`. |
| `random-choice LIST_OR_LINES` | One item, as `random.choice`. |
| `random-choices LIST_OR_LINES K [WEIGHTS]` | K items with replacement, as `random.choices`. WEIGHTS is a list of numbers, one per item. |
| `random-shuffle LIST_OR_LINES` | The items in random order, as a new list; `random.shuffle` reorders in place. |
| `random-sample LIST_OR_LINES K` | K distinct items in random order, as `random.sample`. |

```text
cmdr> random-seed 42
cmdr> repeat 8 { () random-int 1 6 } | join ,
6,1,1,6,3,2,2,2
cmdr> list kick snare hat | random-choices 4 "1\n1\n2"
```

Without `random-seed`, each thread seeds its own generator from `/dev/urandom` on first use. Sessions on one thread share it. Selections return lists, and items may be lists. `repeat N { () ... }` makes N draws, as `[f() for _ in range(n)]` does in Python.

Python's seeding over a smaller generator such as xorshift: scripts can be checked against Python, and a seed means the same in both.
