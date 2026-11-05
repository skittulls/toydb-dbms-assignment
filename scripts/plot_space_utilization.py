#!/usr/bin/env python3
import os
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import pandas as pd
import numpy as np

def plot_space_utilization():
    csv_path = 'results/space_utilization_comparison.csv'
    if not os.path.exists(csv_path):
        csv_path = '../results/space_utilization_comparison.csv'
    
    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found.")
        return

    df = pd.read_csv(csv_path)
    os.makedirs('plots', exist_ok=True)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

    labels = []
    for _, row in df.iterrows():
        if row['Scheme'] == 'SlottedPage':
            labels.append('Slotted Page\n(Dynamic)')
        else:
            labels.append(f"Static\nM={row['MaxRecordLen']}")

    x = np.arange(len(labels))
    colors = ['#1f77b4' if row['Scheme'] == 'SlottedPage' else '#d62728' for _, row in df.iterrows()]

    # 1. Space Utilization (%)
    bars1 = ax1.bar(x, df['SpaceUtilizationPct'], color=colors, width=0.6, edgecolor='black', linewidth=1)
    ax1.set_title('Space Utilization: Slotted Page vs Static Allocation', fontsize=13, fontweight='bold', pad=12)
    ax1.set_xlabel('Record Management Scheme', fontsize=11, labelpad=8)
    ax1.set_ylabel('Effective Space Utilization (%)', fontsize=11, labelpad=8)
    ax1.set_xticks(x)
    ax1.set_xticklabels(labels, fontsize=10)
    ax1.set_ylim(0, 105)
    ax1.grid(axis='y', linestyle='--', alpha=0.5)

    for bar in bars1:
        yval = bar.get_height()
        ax1.text(bar.get_x() + bar.get_width()/2.0, yval + 1.5, f'{yval:.1f}%',
                 ha='center', va='bottom', fontsize=9, fontweight='bold')

    # 2. Total Storage Allocated (KB) & Wasted Space (KB)
    width = 0.35
    alloc_kb = df['AllocatedBytes'] / 1024.0
    waste_kb = df['WastedBytes'] / 1024.0

    b1 = ax2.bar(x - width/2, alloc_kb, width=width, label='Allocated Space (KB)', color='#2ca02c', edgecolor='black')
    b2 = ax2.bar(x + width/2, waste_kb, width=width, label='Wasted Space (KB)', color='#ff7f0e', edgecolor='black')
    ax2.set_title('Total Storage Overhead & Internal Fragmentation', fontsize=13, fontweight='bold', pad=12)
    ax2.set_xlabel('Record Management Scheme', fontsize=11, labelpad=8)
    ax2.set_ylabel('Kilobytes (KB)', fontsize=11, labelpad=8)
    ax2.set_xticks(x)
    ax2.set_xticklabels(labels, fontsize=10)
    ax2.legend(frameon=True, fontsize=10)
    ax2.grid(axis='y', linestyle='--', alpha=0.5)

    plt.tight_layout()
    out_path = 'plots/space_utilization.png'
    plt.savefig(out_path, dpi=300)
    plt.close()
    print(f"Space utilization plot saved to {out_path}")

if __name__ == '__main__':
    plot_space_utilization()
