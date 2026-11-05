#!/usr/bin/env python3
import os
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import pandas as pd
import numpy as np

def plot_am_comparison():
    csv_path = 'results/am_indexing_comparison.csv'
    if not os.path.exists(csv_path):
        csv_path = '../results/am_indexing_comparison.csv'
    
    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found.")
        return

    df = pd.read_csv(csv_path)
    os.makedirs('plots', exist_ok=True)

    fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(19, 6.2))

    if len(df) == 5:
        approaches = [
            'Method 1\nBatch Scan\n(Data+Idx)',
            'Method 2\nIncremental\n(Data+Idx)',
            'Pre-Sorted\nTop-Down\n(Ref Base)',
            'Method 3a\nBulk Load\n(Data+Idx)',
            'Method 3b\nBulk Load\n(Idx Only)'
        ]
        colors = ['#1f77b4', '#ff7f0e', '#9467bd', '#2ca02c', '#17becf']
        width = 0.42
    elif len(df) == 4:
        approaches = [
            'Method 1\nBatch Scan\n(Data+Idx)',
            'Method 2\nIncremental\n(Data+Idx)',
            'Method 3a\nBulk Load\n(Data+Idx)',
            'Method 3b\nBulk Load\n(Idx Only)'
        ]
        colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#17becf']
        width = 0.44
    else:
        approaches = ['Method 1\nBatch Scan', 'Method 2\nIncremental', 'Method 3\nBulk Load']
        colors = ['#1f77b4', '#ff7f0e', '#2ca02c']
        width = 0.52

    x = np.arange(len(approaches))

    # 1. Build Time (ms)
    max_time = float(max(df['BuildTimeMs']))
    b1 = ax1.bar(x, df['BuildTimeMs'], color=colors, width=width, edgecolor='black', linewidth=1)
    ax1.set_title('Index Construction Time', fontsize=12.5, fontweight='bold', pad=14)
    ax1.set_ylabel('Execution Time (ms)', fontsize=11, labelpad=8)
    ax1.set_xticks(x)
    ax1.set_xticklabels(approaches, fontsize=9.5)
    ax1.set_ylim(0, max_time * 1.30)
    ax1.grid(axis='y', linestyle='--', alpha=0.5)
    for bar in b1:
        yval = bar.get_height()
        ax1.text(bar.get_x() + bar.get_width()/2.0, yval + max_time * 0.025, f'{yval:.2f} ms',
                 ha='center', va='bottom', fontsize=9.5, fontweight='bold')

    # 2. Total Disk I/O (Reads vs Writes Stacked)
    p_reads = df['PhysicalReads']
    p_writes = df['PhysicalWrites']
    p_total = df['TotalPhysicalIO']
    max_io = float(max(p_total))

    b_read = ax2.bar(x, p_reads, color='#4682b4', width=width, edgecolor='black', label='Physical Reads')
    b_write = ax2.bar(x, p_writes, bottom=p_reads, color='#d95f02', width=width, edgecolor='black', label='Physical Writes')

    ax2.set_title('Physical Disk I/O (Reads + Writes)', fontsize=12.5, fontweight='bold', pad=14)
    ax2.set_ylabel('Physical Pages Read / Written', fontsize=11, labelpad=8)
    ax2.set_xticks(x)
    ax2.set_xticklabels(approaches, fontsize=9.5)
    ax2.set_ylim(0, max_io * 1.50)
    ax2.grid(axis='y', linestyle='--', alpha=0.5)
    # Legend placed in upper right where bar 4 (104 I/O) leaves ample clear space
    ax2.legend(loc='upper right', frameon=True, framealpha=0.92, edgecolor='#cccccc', fontsize=10)

    for i in range(len(x)):
        tot = p_total.iloc[i]
        rd = p_reads.iloc[i]
        wr = p_writes.iloc[i]
        ax2.text(x[i], tot + max_io * 0.025, f'{tot}\n({rd}R+{wr}W)',
                 ha='center', va='bottom', fontsize=9, fontweight='bold', linespacing=1.15)

    # 3. Storage Footprint: B+ Tree Index Pages vs Data Pages
    idx_pages = df['IndexPages']
    data_pages = df['DataPages'] if 'DataPages' in df.columns else [0, 455, 0]
    total_pages = df['TotalPages'] if 'TotalPages' in df.columns else idx_pages
    max_pages = float(max(total_pages))

    b_idx = ax3.bar(x, idx_pages, color='#2ca02c', width=width, edgecolor='black', label='B+ Tree Index Pages')
    b_data = ax3.bar(x, data_pages, bottom=idx_pages, color='#bcbd22', width=width, edgecolor='black', label='Slotted Data Pages')

    ax3.set_title('Storage Footprint (Index vs Data Pages)', fontsize=12.5, fontweight='bold', pad=14)
    ax3.set_ylabel('Page Count', fontsize=11, labelpad=8)
    ax3.set_xticks(x)
    ax3.set_xticklabels(approaches, fontsize=9.5)
    ax3.set_ylim(0, max_pages * 1.50)
    ax3.grid(axis='y', linestyle='--', alpha=0.5)
    # Legend placed in upper right where bar 4 (101 pages) leaves ample clear space
    ax3.legend(loc='upper right', frameon=True, framealpha=0.92, edgecolor='#cccccc', fontsize=10)

    for i in range(len(x)):
        tot = total_pages.iloc[i]
        idx_p = idx_pages.iloc[i]
        dt_p = data_pages.iloc[i]
        if dt_p > 0:
            lbl = f'{tot} Tot\n({idx_p} idx + {dt_p} data)'
        else:
            lbl = f'{idx_p} idx'
        ax3.text(x[i], tot + max_pages * 0.025, lbl,
                 ha='center', va='bottom', fontsize=9, fontweight='bold', linespacing=1.15)

    plt.tight_layout()
    out_path = 'plots/am_indexing_comparison.png'
    plt.savefig(out_path, dpi=300)
    plt.close()
    print(f"AM indexing comparison plot saved to {out_path}")

if __name__ == '__main__':
    plot_am_comparison()
