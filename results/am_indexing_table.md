# B+ Tree Indexing Evaluation: 3 Approaches on Student roll-no

**Configuration:** Buffer Pool Size = 20 frames (`toydb.cfg`), Leaf Fill Factor = 90%, Total Records = 17813

## 1. End-to-End System Evaluation (Data File + Index File)

| Indexing Approach | Scope | Build Time (ms) | Physical Reads | Physical Writes | Total Disk I/O | Index Pages | Data Pages | Total Pages | Avg Point Query (us) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **1. Batch Single-Op Scan** | Scan Existing Data + Build Index | 16.07 | 548 | 230 | 778 | **135** | 455 | 590 | 1.05 |
| **2. Incremental Interleaved** | Insert Data + Build Index | 16.85 | 93 | 686 | 779 | **135** | 455 | 590 | 0.81 |
| **Pre-Sorted Top-Down (Reference)** | Sorted Keys + Iterative AM_InsertEntry | 4.65 | 1 | 157 | 158 | **177** | 0 | 177 | 1.01 |
| **3a. Bottom-Up Bulk Load** | Scan Pre-Sorted File + Build Index | 6.19 | 456 | 103 | 559 | **101** | 455 | **556** | **0.79** |
| **3b. Bottom-Up Bulk Load** | Index Construction Only | **4.39** | **1** | **103** | **104** | **101** | 0 | **101** | **0.79** |

## 2. Pure B+ Tree Index Component Breakdown

Isolating index-specific operations reveals that dynamic tree construction in Method 1 and Method 2 produces an identical index structure, while Method 3 achieves optimal compaction:

| Indexing Approach | Index Pages | Index Physical Reads | Index Physical Writes | Total Index Disk I/O |
| :--- | :--- | :--- | :--- | :--- |
| **1. Batch Single-Op Scan** | 135 | 93 | 230 | 323 |
| **2. Incremental Interleaved** | 135 | 93 | 231 | 324 |
| **Pre-Sorted Top-Down (Reference Baseline)** | 177 | 1 | 157 | 158 |
| **3. Bottom-Up Bulk Load** | **101** (-25.2%) | **1** | **103** | **104** (-67.9%) |

## 3. Buffer Locality vs Adversarial Thrashing Analysis

| Workload Key Distribution | Construction Mode | Physical Reads | Physical Writes | Total Disk I/O | Buffer Hit Ratio |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Clustered (`student.txt` order)** | Dynamic Insert | 93 | 230 | 323 | 99.48% |
| **Adversarial Uniform Random** | Dynamic Insert | 7,142 | 7,208 | 14,350 | 59.90% |
| **Any Distribution (Sorted)** | **Bulk Loading** | **1** | **103** | **104** | **100.00%** |

> [!NOTE]
> **Pre-Sorted Top-Down Insertion vs. True Bottom-Up Bulk Loading:**
> In conventional iterative insertions on sorted keys (calling `AM_InsertEntry` iteratively in a loop), monotonic key ordering causes leaf pages to undergo right-edge splits where the left leaf is abandoned at ~50% occupancy. Consequently, pre-sorted top-down insertion produces 177 index pages. In contrast, our true bottom-up bulk loader (`AM_BulkLoad`) packs leaves to 90% capacity and builds internal nodes bottom-up without dynamic splits, requiring only 101 index pages and 104 total physical disk I/Os.

> [!NOTE]
> **Identical Index Geometry (135 Index Pages):**
> Both Method 1 and Method 2 insert the exact same 17,813 keys in identical file line order using standard `AM_InsertEntry()`. The resulting B+ tree index is identical in structure and depth, occupying exactly 135 pages. Method 2 additionally allocates 455 slotted data pages to store the student records, yielding 590 total pages across both files.

> [!IMPORTANT]
> **Why Physical Reads are Low in `student.txt` Order (93 Reads vs 7,142 Reads):**
> The `student.txt` dataset is grouped chronologically by admission year prefixes (`95xx`, `96xx`, `97xx`, `98xx`, `99xx`, `00xx`, `01xx`) and departments. Within each chronological batch, 76.4% of consecutive keys are monotonically increasing, concentrating inserts on 8–12 active leaf pages. Because `pool_size = 20`, the root and active cluster fit comfortably in memory, yielding a 99.48% buffer hit ratio. In contrast, under uniform random keys, every insert targets a random leaf among the 134 leaves, causing severe buffer thrashing (14,350 total disk I/Os). Bulk loading remains completely immune to thrashing, requiring only 104 sequential I/Os under all workloads.

> [!TIP]
> **Symmetry Between Method 1 and Method 2 Disk I/O:**
> Method 1 performs 455 physical reads to scan the existing slotted data file plus 93 reads and 230 writes to construct the index (Total: 778 I/O). Method 2 performs 455 physical writes to create the slotted data file plus 93 reads and 231 writes to construct the index (Total: 779 I/O). This confirms that both methods incur virtually identical I/O cost when performing the complete data and index lifecycle.
