#!/usr/bin/env bash
set -e

# ==============================================================================
# Master Automated Test & Evaluation Pipeline
# Platform: Linux / macOS (ARM64 & x86_64)
# ==============================================================================

echo "======================================================================"
echo " Starting Full End-to-End Build, Test, and Benchmark Pipeline"
echo "======================================================================"

# 1. Clean previous build artifacts and temporary files
echo ""
echo "[Step 1/5] Cleaning workspace..."
make clean

# 2. Build libraries and benchmark binaries
echo ""
echo "[Step 2/5] Compiling ToyDB core, slotted-page, bulk-loader, and benchmarks..."
make all

# 3. Execute unit regression tests across PF and AM layers
echo ""
echo "[Step 3/5] Running regression tests..."
make tests

# 4. Execute all 3 performance benchmarks
echo ""
echo "[Step 4/5] Running performance evaluation suites..."
make bench

# 5. Generate publication-quality visualization graphs
echo ""
echo "[Step 5/5] Generating graphical plots..."
make plots

echo ""
echo "======================================================================"
echo " ALL PIPELINE STAGES COMPLETED SUCCESSFULLY!"
echo "======================================================================"
echo "Artifacts generated:"
echo "  - Plots:   plots/pf_io_workload.png"
echo "             plots/space_utilization.png"
echo "             plots/am_indexing_comparison.png"
echo "  - Tables:  results/space_utilization_table.md"
echo "             results/am_indexing_table.md"
echo "  - Metrics: results/pf_workload_metrics.csv"
echo "             results/space_utilization_comparison.csv"
echo "             results/am_indexing_comparison.csv"
echo ""
echo "To package your distribution archive:"
echo "  make pack"
echo "  (creates b24es1034.tar.gz)"
echo "======================================================================"

