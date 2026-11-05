# Space Utilization Comparison: Slotted Page vs. Static Record Management

| Management Scheme | Max Record Len (M) | Total Pages | Allocated (KB) | Useful Data (KB) | Wasted Space (KB) | Internal Frag (%) | Space Utilization (%) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Slotted Page (Dynamic)** | Variable (91-108B) | 455 | 1818.22 | 1717.78 | 100.44 | 0.00% | **94.48%** |
| Static M=108 bytes | 108 bytes | 482 | 1926.12 | 1717.78 | 208.33 | 8.57% | 89.18% |
| Static M=128 bytes | 128 bytes | 575 | 2297.75 | 1717.78 | 579.97 | 22.85% | 74.76% |
| Static M=160 bytes | 160 bytes | 713 | 2849.21 | 1717.78 | 1131.43 | 38.28% | 60.29% |
| Static M=200 bytes | 200 bytes | 891 | 3560.52 | 1717.78 | 1842.74 | 50.63% | 48.25% |
| Static M=256 bytes | 256 bytes | 1188 | 4747.36 | 1717.78 | 3029.58 | 61.43% | 36.18% |
| Static M=512 bytes | 512 bytes | 2545 | 10170.06 | 1717.78 | 8452.27 | 80.71% | 16.89% |
