#!/usr/bin/env python3
"""
plot_pf_workload.py
Generates publication-quality graphs of PF layer I/O statistics across
different mixtures of read and write queries.
# Database Management Systems | Storage & Indexing Engine
"""

import sys
import os

# Use Agg backend for headless / server environments without display servers
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import csv

def main():
    csv_candidates = [
        "results/pf_workload_metrics.csv",
        "../results/pf_workload_metrics.csv",
        "../../results/pf_workload_metrics.csv"
    ]
    csv_file = None
    for cand in csv_candidates:
        if os.path.exists(cand):
            csv_file = cand
            break

    if csv_file is None:
        print("Error: pf_workload_metrics.csv not found. Run ./bin/bench_pf first.")
        sys.exit(1)

    mix_labels = []
    log_reads = []
    log_writes = []
    phys_reads = []
    phys_writes = []
    total_phys = []
    hit_ratios = []

    with open(csv_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            mix_labels.append(row['MixLabel'])
            log_reads.append(int(row['LogicalReads']))
            log_writes.append(int(row['LogicalWrites']))
            phys_reads.append(int(row['PhysicalReads']))
            phys_writes.append(int(row['PhysicalWrites']))
            total_phys.append(int(row['TotalPhysicalIO']))
            hit_ratios.append(float(row['HitRatio']))

    out_dir = os.path.dirname(csv_file).replace("results", "plots")
    if not os.path.exists(out_dir):
        out_dir = "plots"
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "pf_io_workload.png")

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5.5), dpi=300)

    # Subplot 1: I/O Operations vs Query Mixture
    ax1.plot(mix_labels, log_reads, marker='o', color='#1f77b4', linewidth=2, label='Logical Reads')
    ax1.plot(mix_labels, log_writes, marker='s', color='#ff7f0e', linewidth=2, label='Logical Writes')
    ax1.plot(mix_labels, phys_reads, marker='^', color='#2ca02c', linewidth=2, linestyle='--', label='Physical Reads')
    ax1.plot(mix_labels, phys_writes, marker='d', color='#d62728', linewidth=2, linestyle='--', label='Physical Writes')

    ax1.set_title('Logical & Physical I/O vs. Read/Write Query Mix', fontsize=12, fontweight='bold', pad=10)
    ax1.set_xlabel('Query Mixture (% Read / % Write)', fontsize=11, fontweight='bold')
    ax1.set_ylabel('I/O Operations Count', fontsize=11, fontweight='bold')
    ax1.set_ylim(0, max(log_reads) * 1.25)
    ax1.grid(True, linestyle=':', alpha=0.6)
    ax1.legend(loc='upper right', frameon=True, framealpha=0.92, fontsize=9.5)

    # Subplot 2: Total Physical I/O & Hit Ratio
    color = '#9467bd'
    ax2.set_title('Total Disk I/O & Buffer Hit Ratio', fontsize=12, fontweight='bold', pad=10)
    ax2.set_xlabel('Query Mixture (% Read / % Write)', fontsize=11, fontweight='bold')
    ax2.set_ylabel('Total Physical I/O (Disk Reads + Writes)', color=color, fontsize=11, fontweight='bold')
    bar = ax2.bar(mix_labels, total_phys, color=color, alpha=0.45, width=0.45, label='Total Physical I/O')
    ax2.set_ylim(0, max(total_phys) * 1.30)
    ax2.tick_params(axis='y', labelcolor=color)
    ax2.grid(True, linestyle=':', alpha=0.6)

    ax2_twin = ax2.twinx()
    color_twin = '#e377c2'
    line_twin = ax2_twin.plot(mix_labels, hit_ratios, color=color_twin, marker='o', linewidth=2.5, label='Buffer Hit Ratio (%)')
    ax2_twin.set_ylabel('Buffer Hit Ratio (%)', color=color_twin, fontsize=11, fontweight='bold')
    ax2_twin.tick_params(axis='y', labelcolor=color_twin)
    ax2_twin.set_ylim([0, 100])

    lines1, labels1 = ax2.get_legend_handles_labels()
    lines2, labels2 = ax2_twin.get_legend_handles_labels()
    ax2.legend(lines1 + lines2, labels1 + labels2, loc='upper left', frameon=True, framealpha=0.92, fontsize=9.5)

    fig.tight_layout()
    plt.savefig(out_path, bbox_inches='tight')
    plt.close()

    print(f"Graph successfully generated and saved to {out_path}")

if __name__ == '__main__':
    main()

