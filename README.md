# CSE325-G4-kitchen

## Restaurant Kitchen: Chefs and Shared Equipment

### Build

To compile the project, run:

```bash
make
```

The project must compile using GCC with the following flags:

```bash
-Wall -Wextra -O0 -g
```

The build must complete without warnings.

To remove compiled files and leftover IPC objects:

```bash
make clean
```

### Run

Run the default demonstration:

```bash
make run
```

Run the automated tests:

```bash
make test
```

Run a specific synchronization mode:

```bash
./kitchen --mode=ordered
```

Available synchronization modes:

| Mode | Description |
|---|---|
| `nosync` | Runs without synchronization (baseline). |
| `naive` | Acquires the left item first, followed by the right item. |
| `ordered` | Acquires the lower-numbered item first. |
| `room` | Allows at most N − 1 chefs to compete for equipment simultaneously. |
| `sysv` | Acquires both items atomically using System V semaphores. |
| `monitor` | Uses the dining philosophers monitor implemented with semaphores. |

### Command-Line Options

| Option | Description | Default |
|---|---|---|
| `--mode=NAME` | Selects the synchronization mode. | — |
| `--seconds=T` | Runs the simulation for T seconds. | 5 |
| `--items=N` | Runs until N work items are completed. | — |
| `--seed=S` | Sets the random seed. | 325 |
| `--csv=FILE` | Appends one result row to the specified CSV file. | — |
| `--verbose` | Prints every simulation event. | Off |
| `--chefs=N` | Sets the number of chefs and equipment items. | 5 |
| `--reach-ms=X` | Sets the delay between acquiring the first and second equipment items. | 1 ms |
| `--items-per-dish=K` | Sets the number of equipment items required per dish. | 2 |

### Example Commands

Run the ordered mode with 5 chefs for 10 seconds:

```bash
./kitchen --mode=ordered --chefs=5 --seconds=10
```

Run the monitor mode with verbose output:

```bash
./kitchen --mode=monitor --verbose
```

Run the System V mode and save results to a CSV file:

```bash
./kitchen --mode=sysv --seconds=10 --csv=results/test.csv
```

Run the monitor mode with 3 equipment items per dish:

```bash
./kitchen --mode=monitor --items-per-dish=3
```

### Sample Output

```text
[Actual program output will be added after implementation.]

CHECK exclusive: PASS
CHECK no_deadlock: PASS
CHECK all_ate: PASS
CHECK counts: PASS

[Final statistics will be displayed here.]
```

---

**CSE325 | Operating Systems | Group 4**
**East West University | Fall 2026**
