# time

The current time, and times formatted with strftime(3).

| Command | Result |
|-|-|
| `time.now` | The current time, in whole seconds since the epoch. |
| `time.local [EPOCH] FORMAT` | EPOCH, default now, formatted in local time. `TZ` sets the zone. |
| `time.utc [EPOCH] FORMAT` | EPOCH, default now, formatted in UTC. |
| `time.today` | Today's local date, as `YYYY-MM-DD`. |

EPOCH comes first, so a time can be piped in:

```text
cmdr> import time
cmdr> import fs
cmdr> fs.mtime README.md | time.local '%Y-%m-%d %H:%M'
2026-10-18 09:41
cmdr> time.utc 0 %c
Thu Jan  1 00:00:00 1970
```

FORMAT takes strftime's conversions, such as `%Y`, `%m`, `%d`, `%H`, `%M`, and `%S`. Names, such as `%a` and `%b`, are in English, since cmdr does not set a locale. Output over 65,536 bytes is an error.

`sh 'date ...'` runs a process per call, and formatting a given time needs `date -r` on BSD but `date -d` on GNU.
