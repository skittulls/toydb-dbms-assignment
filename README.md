# ToyDB Storage & Index Engine: Extended Architecture & Performance Evaluation

**Author**: Vidhi Sharma (B24ES1034)  
**Academic Affiliation**: Database Management Systems | Prof. Nitin Awathare  
**Architecture**: Paged File (PF) Layer Buffer Manager, Slotted-Page Variable-Length Record Manager, B+ Tree Access Method (AM) Layer Bulk Loader  
**Platform**: Cross-platform C89/GNU89/C99 (macOS Clang on ARM64/x86_64 and Linux GCC)  
**Dataset**: `data/student.txt` (17,813 records)  

---

## 1. Executive Summary

This repository provides an industrial-grade, robust C implementation extending **ToyDB** across three core database engine layers:
1. **Module 1 (PF Layer Page Buffering & Strategy Management)**: Implemented configurable buffer pool management with file-selectable replacement policies (`PF_STRATEGY_LRU` and `PF_STRATEGY_MRU`), active dirty frame tracking via `PF_MarkDirty`, fair-quota victim selection ($\max(3, \lfloor \text{pool\_size}/\text{open\_files} \rfloor)$), cross-file steal via global LRU, and rigorous accounting of logical/physical I/O across 7 read/write query mixtures ($100/0$ down to $0/100$).
2. **Module 2 (Slotted-Page Record Management)**: Engineered dual-direction slotted-page storage over native 4,092-byte `PF_PAGE_SIZE` payloads with 4-byte packed record identifiers (`RID_TO_INT`), tombstone slot reuse, fast $O(1)$ page allocation, and in-place defragmentation (`SP_CompactPage`). Evaluated space utilization across 17,813 variable-length student records against static fixed-slot allocation over $M \in \{108, 128, 160, 200, 256, 512\}$.
3. **Module 3 (B+ Tree AM Layer Indexing & Bulk Loading)**: Implemented and evaluated three indexing approaches on `roll-no`:
   - **Method 1 (Batch Single-Op Scan)**: Sequential scan over existing slotted pages followed by batch index insertion.
   - **Method 2 (Incremental Interleaved)**: Simultaneous interleaved record insert into slotted pages and index insert.
   - **Method 3 (True Bottom-Up Bulk Loading)**: Optimal $O(N \log N)$ sorting, Page 0 root reservation, duplicate key linked-list compaction, and bottom-up internal node construction with subtree minimum key propagation.
   Cross-validated all three trees with **500 point queries** (100% exact RID matches) and **range queries** (100% result equivalence).

All parameters are dynamically loaded from `toydb.cfg` at startup, enabling complete runtime configurability without code modification or recompilation.

---

## 2. Directory Structure

```text
.
├── Makefile                     # Root master build system (all, tests, bench, plots, clean, pack)
├── README.md                    # Comprehensive documentation and technical report
├── toydb.cfg                    # Central configuration file (Zero-code-edit evaluation)
├── bin/                         # Compiled benchmark binaries
│   ├── bench_pf                 # Module 1 buffer manager & workload benchmark
│   ├── bench_slotted            # Module 2 slotted-page & space utilization benchmark
│   └── bench_am                 # Module 3 B+ tree indexing & cross-validation benchmark
├── data/                        # Datasets
│   └── student.txt              # 17,813 student records (semicolon-delimited)
├── plots/                       # Publication-quality visualization graphs
│   ├── pf_io_workload.png       # Module 1: Logical vs Physical I/O over Read/Write mixtures
│   ├── space_utilization.png    # Module 2: Space utilization & fragmentation across M
│   └── am_indexing_comparison.png # Module 3: Build time, physical I/O, and page counts
├── results/                     # Exported raw metrics and markdown comparison tables
│   ├── pf_workload_metrics.csv
│   ├── space_utilization_comparison.csv
│   ├── space_utilization_table.md
│   ├── am_indexing_comparison.csv
│   └── am_indexing_table.md
├── scripts/                     # Helper execution and plotting scripts
│   ├── run_all.sh               # Master one-click end-to-end build, test, and evaluation script
│   ├── plot_pf_workload.py      # Plotter for PF layer metrics
│   ├── plot_space_utilization.py# Plotter for slotted vs static record metrics
│   └── plot_am_comparison.py    # Plotter for B+ tree indexing metrics
└── toydb/                       # Core database source code
    ├── common/                  # Configuration auto-loader (config.h, config.c)
    ├── pflayer/                 # Extended Paged File layer (buf.c, pf.c, hash.c, pf.h, pftypes.h)
    ├── amlayer/                 # Extended Access Method layer (ambulk.c, amfns.c, amsearch.c, am.h)
    ├── slotted/                 # Slotted-page subsystem (slotted.c, student_loader.c, static_record.c)
    └── bench/                   # Benchmark harness implementations (bench_pf.c, bench_slotted.c, bench_am.c)
```

---

## 3. Compilation & Execution Instructions

### Prerequisites
- Standard C compiler (`clang` or `gcc`) supporting C89/GNU89.
- GNU Make.
- Python 3 with `matplotlib` and `pandas` (for plotting).

### Fast-Track One-Click Pipeline
To cleanly build libraries, run all regression tests, execute all benchmarks, and generate plots in one step:
```bash
./scripts/run_all.sh
```

### Manual Step-by-Step Compilation
1. **Compile all libraries and benchmark binaries**:
   ```bash
   make all
   ```
2. **Execute regression test suite** (Verifies PF buffer semantics, hash table, and AM B+ tree split/scan/delete operations):
   ```bash
   make tests
   ```
3. **Execute performance evaluation benchmarks**:
   ```bash
   make bench
   ```
4. **End-to-End Student Lookup Demo**:
   Search the B+ tree index for any roll number, unpack the 32-bit `recId` into `(pageNum, slotNum)`, fetch the variable-length record payload from the slotted-page file, and inspect page I/O:
   ```bash
   ./bin/bench_am --query 95302001
   # Or search alphanumeric keys (when key_type = char)
   ./bin/bench_am --query 96D07001
   ```
   *Note*: The target student roll number can also be configured permanently in `toydb.cfg` via `demo_query_rollno`.
5. **Generate graphical charts**:
   ```bash
   make plots
   ```
6. **Clean compiled objects and temporary test databases**:
   ```bash
   make clean
   ```
7. **Package project archive**:
   ```bash
   make pack
   # Or explicitly: make pack ROLLNO=b24es1034
   # Creates b24es1034.tar.gz
   ```

---

## 4. Architectural Design & Implementation Details

### 4.1 Module 1: PF Layer Buffer Manager
- **Exact Block Sizing**: `PF_PAGE_SIZE` is defined as `4092` bytes across both `toydb/pflayer/pf.h` and `toydb/amlayer/pf.h`. Since `PFfpage` adds a 4-byte `nextfree` header, each page occupies exactly $4 + 4092 = 4096$ bytes on disk (1 OS block).
- **Clean Pin Counter Semantics**: `fixed` in `struct PFbpage` is declared as full `short fixed;` (replacing bug-prone signed 1-bit bitfields `short fixed:1;` which overflowed to -1 on 64-bit systems). `PFbufGet` enforces `if (bpage->fixed) return PFE_PAGEFIXED;` to guarantee full compliance with ToyDB test specifications.
- **Fair-Quota Victim Selection**: Inside `SelectVictim` (`buf.c`), victim eviction enforces:
  $$\text{Fair Quota} = \max\left(3, \left\lfloor \frac{\text{pool\_size}}{\text{num\_open\_files}} \right\rfloor\right)$$
  If the active file has at least its fair quota of frames, eviction strictly follows the file's chosen policy (`PF_STRATEGY_LRU` or `PF_STRATEGY_MRU`). If the file has fewer frames, the buffer manager steals from other files using global **LRU** (evicting the coldest frame), ensuring that high-throughput MRU sequential scans never evict hot index pages belonging to other open files.

```text
                         New Buffer Page Allocation Request
                                         |
                                         v
                            [ Free Frame in Pool? ]
                                   /        \
                             YES  /          \  NO (Pool Saturated)
                                 v            v
                         Reuse Free Frame   Compute Target Fair Quota:
                                            fair_quota = max(3, pool_size / open_files)
                                                      |
                                                      v
                                      [ target_fd frames >= fair_quota? ]
                                             /                 \
                                       YES  /                   \  NO
                                           v                     v
                             Evict from target_fd        Steal from other open files
                             using file's OWN policy:    using GLOBAL LRU:
                             - PF_STRATEGY_MRU:          - Evicts coldest frame
                               Scans for max timestamp     across all other files
                             - PF_STRATEGY_LRU:          - Protects hot index roots
                               Scans for min timestamp     from sequential scan flush
```

- **Single-Point I/O Accounting**:
  - `logical_reads`: Incremented exclusively within `PFbufGet()`, guaranteeing the strict DBMS invariant:
    $$\text{logical\_reads} = \text{buffer\_hits} + \text{buffer\_misses}$$
  - `logical_writes`: Incremented on `PF_MarkDirty()` and in `PFbufUnfix(fd, pageNum, dirty)` only when `dirty == TRUE` and the frame was previously clean.
  - `physical_writes`: Incremented only upon physical dirty-page eviction in `PFbufInternalAlloc()` and upon file closure in `PFbufReleaseFile()`.

### 4.2 Module 2: Slotted-Page Variable-Length Record Layer
- **Physical Layout**: Dual-direction growth inside each 4,092-byte page:
  - **Header & Slot Array**: Header (`SPHeader`, 8 bytes) and slot directories (`SPSlot`, 4 bytes: `offset`, `length`) grow forward from offset 0.
  - **Record Payloads**: Variable-length student records grow backward from `PF_PAGE_SIZE` towards the center.
  - **Free Space Pointer**: Tracked by `freeSpaceOffset`, pointing to the lowest byte of the top record payload.

```text
+-----------------------------------------------------------------------------------+
|                           PF_PAGE_SIZE = 4092 Bytes                               |
|                                                                                   |
|  [ SPHeader (8B) ]  [ Slot Directory Entry (4B each) ]   ===>                      |
|  +----------------+  +-----------------+-----------------+                        |
|  | numSlots       |  | Slot 0 (4B)     | Slot 1 (4B)     |                        |
|  | freeSpaceOffset|  | offset | length | offset | length |                        |
|  | freeSpaceBytes |  +-----------------+-----------------+                        |
|  | numRecords     |  (Grows Forward: offset 8 -> 12 -> 16...)                     |
|  +----------------+                                                               |
|                                                                                   |
|                        FREE CONTIGUOUS SPACE GAP                                  |
|                                                                                   |
|                      <=== (Grows Backward: 4092 <- 3994 <- 3896...)               |
|                      +------------------------+------------------------+          |
|                      | Record 1 Payload       | Record 0 Payload       |          |
|                      +------------------------+------------------------+          |
+-----------------------------------------------------------------------------------+

32-Bit Bitpacked RID Format (AM Layer recId Compatible):
+------------------------------------+------------------------------------+
|       pageNum (High 16 Bits)       |       slotNum (Low 16 Bits)        |
|               0x0000               |               0x0004               |
+------------------------------------+------------------------------------+
Macros:
  RID_TO_INT(r)  = (((r).pageNum & 0xFFFF) << 16) | ((r).slotNum & 0xFFFF)
  INT_TO_PAGE(x) = ((x) >> 16) & 0xFFFF
  INT_TO_SLOT(x) = (short)((x) & 0xFFFF)
```

- **Tombstone Slot Reuse**: When a record is deleted, its slot length is set to `-1` (tombstone) and space is added to `freeSpaceBytes`. Future inserts scan for existing tombstones before allocating a new slot, preventing slot directory growth.
- **In-Place Compaction (`SP_CompactPage`)**: Triggered when $N_{\text{needed}} \le \text{freeSpaceBytes}$ but contiguous free space in the center is fragmented by deleted records. All valid records are compacted backward against `PF_PAGE_SIZE` and slot offsets are updated in a single pass without allocating auxiliary heap memory.
- **4-Byte Packed Record Identifiers**: Packed using:
  ```c
  #define RID_TO_INT(r)  ((((r).pageNum & 0xFFFF) << 16) | ((r).slotNum & 0xFFFF))
  #define INT_TO_PAGE(x) (((x) >> 16) & 0xFFFF)
  #define INT_TO_SLOT(x) ((short)((x) & 0xFFFF))
  ```

### 4.3 Module 3: B+ Tree Access Method & Bulk Loading
- **Method 1 (Batch Single-Operation Scan)**: Scans all 17,813 records from the base slotted-page file and inserts each key/RID pair into the B+ tree via standard `AM_InsertEntry`.
- **Method 2 (Incremental Interleaved)**: Simultaneously inserts variable-length student records into an empty slotted-page file and incrementally inserts the key and newly generated RID into the index.
- **Reference Baseline (Pre-Sorted Iterative Top-Down)**: Conventional sorted iterative insertion (sorting keys and calling `AM_InsertEntry` iteratively). Due to monotonic insertion causing right-edge splits with ~50% leaf occupancy, this produces 177 pages and requires standard top-down descent.
- **Method 3 (True Bottom-Up Bulk Loading)**:
  1. **In-Memory Sort**: Sorts all $(key, RID)$ pairs in $O(N \log N)$ time.
  2. **Page 0 Reservation**: Allocates Page 0 upfront to guarantee that the final B+ tree root resides at Page 0 (matching ToyDB's `PF_GetFirstPage` root lookup).
  3. **Packed Leaf Generation**: Packs leaf pages up to the configured fill factor (90%). Multiple RIDs for duplicate keys are stored in a contiguous linked list at the top of the leaf page (`recIdPtr` chain) without duplicating keys or splitting across leaf boundaries.
  4. **Subtree Minimum Key Propagation**: Recursively constructs internal index levels bottom-up. For Level 2+ internal nodes, the separator key stored in parent node $P$ before child pointer $C_i$ is `children[i].subtreeMinKey` (the smallest key in the leftmost leaf of $C_i$'s entire subtree), ensuring correct routing.
  5. **Root Normalization**: Copies the top-level root node into Page 0 and disposes the temporary page.

```text
Level 2 (Root Normalized to Page 0):
                 +-------------------------------------------------------------+
                 | AM_INTHEADER | Child: Pg 101 | '96D07001' | Child: Pg 102   |
                 +-------------------------------------------------------------+
                     /                                           \
                    /                                             \
Level 1 (Internal):v                                               v
   +------------------------------------+             +------------------------------------+
   | AM_INTHEADER | Pg 1 | ... | Pg 10  |             | AM_INTHEADER | Pg 11 | ... | Pg 20 |
   +------------------------------------+             +------------------------------------+
      /            \                                      /             \
     v              v                                    v               v
Level 0 (Leaves, Packed to 90% Fill Factor):
+-------------------------+   nextLeafPage   +-------------------------+
| AM_LEAFHEADER           | ---------------> | AM_LEAFHEADER           |
| Key: 95302001 -> recId  |                  | Key: 96D07001 -> recId  |
| Key: 95302002 -> recId  |                  | Key: 96D07002 -> recId  |
+-------------------------+                  +-------------------------+
 (Page 1)                                     (Page 2)

Step 1: Reserve Page 0 upfront (PF_AllocPage).
Step 2: Pack sorted leaves sequentially (Pages 1, 2, ...).
Step 3: Recursively build internal levels propagating subtreeMinKey.
Step 4: Root Normalization: Copy the final top-level root node buffer directly into Page 0.
```

---

## 5. Experimental Results & Analysis

### 5.1 Module 1: Buffer Management & Workload Sensitivity
- **Cyclic Sequential Scan (Working Set: 25 pages, Pool: 20 frames, 10 passes)**:
  | Replacement Strategy | Logical Reads | Buffer Hits | Buffer Misses | Physical Reads | Hit Ratio |
  | :--- | :--- | :--- | :--- | :--- | :--- |
  | **LRU** | 250 | 0 | 250 | 250 | **0.00%** |
  | **MRU** | 250 | 180 | 70 | 70 | **72.00%** |

  *Insight*: LRU experiences total buffer flooding ($0\%$ hit ratio) because every page read evicts the page that will be referenced earliest in the next cycle. In contrast, MRU evicts the page most recently scanned, locking 19 hot frames into memory and achieving $72\%$ hit ratio (near theoretical upper bound of $19/25 = 76\%$).

- **Read/Write Workload Mixtures (Working Set: 100 pages, Pool: 20 frames, 2,000 queries)**:
  | Query Mixture (R/W) | Logical Reads | Logical Writes | Physical Reads | Physical Writes | Total Physical I/O | Buffer Hit Ratio |
  | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
  | **100 / 0** | 2,000 | 0 | 1,587 | 0 | 1,587 | 20.65% |
  | **80 / 20** | 2,000 | 393 | 1,608 | 393 | 2,001 | 19.60% |
  | **60 / 40** | 2,000 | 726 | 1,580 | 726 | 2,306 | 21.00% |
  | **50 / 50** | 2,000 | 892 | 1,630 | 892 | 2,522 | 18.50% |
  | **40 / 60** | 2,000 | 988 | 1,577 | 988 | 2,565 | 21.15% |
  | **20 / 80** | 2,000 | 1,335 | 1,595 | 1,335 | 2,930 | 20.25% |
  | **0 / 100** | 2,000 | 1,602 | 1,602 | 1,602 | 3,204 | 19.90% |

  *Insight*: Buffer hit ratio remains stable around $\sim 20\%$ (the ratio of buffer pool size $20$ to working set $100$). Total physical disk I/O scales linearly with write percentage as dirty frames are flushed upon victim replacement.

---

### 5.2 Module 2: Space Utilization Comparison (17,813 Student Records)
- **Dataset Metrics**: Minimum length = 91 bytes, Maximum length = 108 bytes, Mean length = 98.75 bytes.

| Record Management Scheme | Slot Size (M) | Total Pages | Allocated Storage | Useful Payload | Wasted Space | Internal Fragmentation | Space Utilization |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Slotted Page (Dynamic)** | **Variable (91–108B)** | **455** | **1,818.22 KB** | **1,717.78 KB** | **100.44 KB** | **0.00%** | **94.48%** |
| Static Allocation | $M = 108$ bytes | 482 | 1,926.12 KB | 1,717.78 KB | 208.33 KB | 8.57% | 89.18% |
| Static Allocation | $M = 128$ bytes | 575 | 2,297.75 KB | 1,717.78 KB | 579.97 KB | 22.85% | 74.76% |
| Static Allocation | $M = 160$ bytes | 713 | 2,849.21 KB | 1,717.78 KB | 1,131.43 KB | 38.28% | 60.29% |
| Static Allocation | $M = 200$ bytes | 891 | 3,560.52 KB | 1,717.78 KB | 1,842.74 KB | 50.63% | 48.25% |
| Static Allocation | $M = 256$ bytes | 1,188 | 4,747.36 KB | 1,717.78 KB | 3,029.58 KB | 61.43% | 36.18% |
| Static Allocation | $M = 512$ bytes | 2,545 | 10,170.06 KB | 1,717.78 KB | 8,452.27 KB | 80.71% | 16.89% |

*Key Findings*:
1. **Slotted Page Superiority**: Achieves **94.48%** effective space utilization with zero internal fragmentation.
2. **Fixed-Slot Degeneracy**: Static record management wastes up to **80.71%** of disk storage to internal padding when $M = 512$, requiring 2,545 pages (over $5.5\times$ more storage than slotted-page management).

---

### 5.3 Module 3: B+ Tree Indexing Comparison & Query Cross-Validation
Evaluated on `student.txt` using `roll-no` key across all 17,813 records (Buffer Pool Size = 20 frames from `toydb.cfg`, Leaf Fill Factor = 90%):

#### Table 1: End-to-End System Evaluation (Data File + Index File)
| Indexing Approach | Scope | Build Time (ms) | Physical Reads | Physical Writes | Total Disk I/O | Index Pages | Data Pages | Total Pages | Avg Point Query (us) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **1. Batch Single-Op Scan** | Scan Existing Data + Build Index | 15.53 ms | 548 | 230 | 778 | **135** | 455 | 590 | 0.97 us |
| **2. Incremental Interleaved** | Insert Data + Build Index | 16.21 ms | 93 | 686 | 779 | **135** | 455 | 590 | 0.80 us |
| **3a. Bottom-Up Bulk Load** | Scan Pre-Sorted File + Build Index | 6.52 ms | 456 | 103 | 559 | **101** | 455 | 556 | 0.81 us |
| **3b. Bottom-Up Bulk Load** | Index Construction Only | **4.72 ms** | **1** | **103** | **104** | **101** | 0 | **101** | **0.81 us** |

#### Table 2: Pure B+ Tree Index Component Breakdown (Excluding Data Pages)
Isolating index-specific operations demonstrates that dynamic tree construction in Method 1 and Method 2 produces an identical index structure, while Method 3 achieves optimal compaction:
| Indexing Approach | Index Pages | Index Physical Reads | Index Physical Writes | Total Index Disk I/O |
| :--- | :--- | :--- | :--- | :--- |
| **1. Batch Single-Op Scan** | 135 | 93 | 230 | 323 |
| **2. Incremental Interleaved** | 135 | 93 | 231 | 324 |
| **3. Bottom-Up Bulk Load** | **101** (-25.2%) | **1** | **103** | **104** (-67.9%) |

#### Table 3: Buffer Locality vs Adversarial Thrashing Analysis (`pool_size = 20`)
| Workload Key Distribution | Construction Mode | Physical Reads | Physical Writes | Total Disk I/O | Buffer Hit Ratio |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Clustered (`student.txt` order)** | Dynamic Insert | 93 | 230 | 323 | 99.48% |
| **Adversarial Uniform Random** | Dynamic Insert | 7,142 | 7,208 | 14,350 | 59.90% |
| **Any Distribution (Sorted)** | **Bulk Loading** | **1** | **103** | **104** | **100.00%** |

- **Cross-Validation Verification**:
  - **Point Queries**: Tested across 500 pseudo-random keys. Result: **500 / 500 exact matches** across all three trees ($100\%$ equivalence).
  - **Range Queries** (`GREATER_THAN_EQUAL` scan): Method 1 retrieved 6,112 records, Method 3 retrieved 6,112 records (**100% Exact Match: PASSED**).

*Architectural & Empirical Insights*:
1. **Identical Index Geometry (135 Index Pages)**:
   - Both Method 1 and Method 2 insert the exact same 17,813 keys in identical file line order using standard `AM_InsertEntry()`. The resulting B+ tree index is identical in structure and depth, occupying exactly **135 pages**.
   - Method 1 and Method 2 both handle **455 slotted data pages** (scanned in Method 1, written in Method 2), resulting in **590 total pages** across both files.
2. **Symmetry Between Method 1 and Method 2 Total Disk I/O (778 vs 779)**:
   - In Method 1: 455 reads are for scanning the existing slotted data file, while 93 reads and 230 writes construct the index ($455 + 93 + 230 = 778$ total I/O).
   - In Method 2: 455 writes are for creating the slotted data file, while 93 reads and 231 writes construct the index ($455 + 93 + 231 = 779$ total I/O).
   - Both methods incur virtually identical total disk I/O when performing the full data and index lifecycle under 20-frame buffer pool contention.
3. **Buffer Locality in `student.txt` vs Buffer Thrashing in Random Workloads (Why 93 Reads vs 7,142 Reads)**:
   - The `student.txt` dataset is grouped chronologically by admission year prefixes (`95xx`, `96xx`, `97xx`, `98xx`, `99xx`, `00xx`, `01xx`) and departments. Within each chronological batch, 76.4% of consecutive keys are monotonically increasing, concentrating inserts on 8–12 active leaf pages. Because `pool_size = 20`, the root and active cluster fit comfortably in memory, yielding a 99.48% buffer hit ratio.
   - When keys are uniformly randomized across the 134 leaves, dynamic tree insertion causes catastrophic buffer thrashing (**7,142 physical reads + 7,208 physical writes = 14,350 disk I/Os**).
   - Bulk loading remains completely immune to thrashing, requiring only **104 sequential disk I/Os** under all workloads — a **138x reduction in physical I/O** over random dynamic insertion!
4. **Bulk Loading Compaction & Throughput**:
   - Bottom-up leaf packing at 90% fill factor compacts the tree into **101 pages** (a **25.2% storage saving** over 50/50 dynamic splits).
   - Construction time is **4.72 ms** (over **$3.4\times$ faster** than incremental interleaved insertion).

---

## 6. Configuration Guide (`toydb.cfg`)

All operational thresholds, buffer pool parameters, and benchmark configurations are dynamically managed via `toydb.cfg` without modifying or recompiling the codebase:

```ini
[buffer_pool]
# Dynamic size of buffer pool (number of frames)
pool_size = 20

# Default file replacement policy (0 = LRU, 1 = MRU)
default_strategy = 0

[workload]
# Working set page count for random access mixtures
working_set_pages = 100

# File page count for cyclic sequential access scan
cyclic_scan_pages = 25

# Total queries executed in benchmark
num_queries = 2000

# Tested query mixtures (Read% / Write%)
query_mixes = 100/0, 80/20, 60/40, 50/50, 40/60, 20/80, 0/100

[slotted_page]
student_file = data/student.txt
static_thresholds = 108, 128, 160, 200, 256, 512

[bulk_load]
# Target leaf packing fill factor percentage
leaf_fill_factor = 90

# Key mode: "char" (all 17,813 records) or "numeric" (16,977 records)
key_type = char
```

---

## 7. System Verification & Engine Invariants

The engine undergoes rigorous multi-layer verification to guarantee architectural correctness, memory safety, and high-throughput stability:

- **Regression Test Suite**: Passes 100% of underlying ToyDB test suites (`testpf`, `testhash`, `test1`, `test2`, `test3`), verifying block-level durability, chained bucket hashing, and multi-level B+ tree splits.
- **Strict Memory & Alignment Safety**: Dual-direction slotted page layout conforms strictly to native 4,092-byte `PF_PAGE_SIZE` with 4-byte page header alignment, eliminating buffer overruns and memory corruption.
- **Cache Eviction Invariants**: Cyclic sequential scanning demonstrates MRU achieving 72.00% hit ratio vs LRU 0.00% hit ratio, eliminating sequential buffer flooding.
- **Index Equivalence & Integrity**: 100% RID equivalence across 500 point queries and range scans between dynamic tree insertion and bottom-up bulk loading.
- **Dynamic Runtime Configuration**: Centralized configuration parser (`toydb/common/config.c`) dynamically binds evaluation parameters from `toydb.cfg` at startup.
- **Build Portability**: Clean compilation across Linux and macOS architectures (ARM64 and x86_64) using GCC and Clang toolchains.

