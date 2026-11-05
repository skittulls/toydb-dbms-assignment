# ==============================================================================
# ToyDB Extensions & Performance Benchmarks
# Student: Vidhi Sharma (B24ES1034)
# Platform: Linux / macOS (ARM64 & x86_64)
# ==============================================================================

CC ?= cc
CFLAGS = -std=gnu89 -O2 -Wall -Wno-implicit-function-declaration -Wno-implicit-int -Wno-return-type \
         -Itoydb/pflayer -Itoydb/amlayer -Itoydb/slotted -Itoydb/common

ROLLNO ?= b24es1034

BIN_DIR = bin
RESULTS_DIR = results
PLOTS_DIR = plots

PF_LIB = toydb/pflayer/pflayer.o
AM_LIB = toydb/amlayer/amlayer.o
SLOTTED_OBJS = toydb/slotted/slotted.o toydb/slotted/student_loader.o toydb/slotted/static_record.o

BENCHMARKS = $(BIN_DIR)/bench_pf $(BIN_DIR)/bench_slotted $(BIN_DIR)/bench_am

.PHONY: all tests bench plots clean pack help

all: dirs $(PF_LIB) $(AM_LIB) $(SLOTTED_OBJS) $(BENCHMARKS)

dirs:
	@mkdir -p $(BIN_DIR) $(RESULTS_DIR) $(PLOTS_DIR)

# ------------------------------------------------------------------------------
# Layer Libraries
# ------------------------------------------------------------------------------
$(PF_LIB):
	@$(MAKE) -C toydb/pflayer pflayer.o

$(AM_LIB):
	@$(MAKE) -C toydb/amlayer amlayer.o

toydb/slotted/slotted.o: toydb/slotted/slotted.c toydb/slotted/slotted.h toydb/pflayer/pf.h
	$(CC) $(CFLAGS) -c toydb/slotted/slotted.c -o toydb/slotted/slotted.o

toydb/slotted/student_loader.o: toydb/slotted/student_loader.c toydb/slotted/student_loader.h toydb/slotted/slotted.h
	$(CC) $(CFLAGS) -c toydb/slotted/student_loader.c -o toydb/slotted/student_loader.o

toydb/slotted/static_record.o: toydb/slotted/static_record.c toydb/slotted/static_record.h
	$(CC) $(CFLAGS) -c toydb/slotted/static_record.c -o toydb/slotted/static_record.o

# ------------------------------------------------------------------------------
# Benchmark Executables
# ------------------------------------------------------------------------------
$(BIN_DIR)/bench_pf: toydb/bench/bench_pf.c $(PF_LIB) toydb/common/config.h
	$(CC) $(CFLAGS) toydb/bench/bench_pf.c $(PF_LIB) -o $(BIN_DIR)/bench_pf

$(BIN_DIR)/bench_slotted: toydb/bench/bench_slotted.c $(SLOTTED_OBJS) $(PF_LIB)
	$(CC) $(CFLAGS) toydb/bench/bench_slotted.c $(SLOTTED_OBJS) $(PF_LIB) -o $(BIN_DIR)/bench_slotted

$(BIN_DIR)/bench_am: toydb/bench/bench_am.c toydb/amlayer/ambulk.c $(SLOTTED_OBJS) $(AM_LIB) $(PF_LIB)
	$(CC) $(CFLAGS) toydb/bench/bench_am.c toydb/amlayer/ambulk.c $(SLOTTED_OBJS) $(AM_LIB) $(PF_LIB) -o $(BIN_DIR)/bench_am

# ------------------------------------------------------------------------------
# Regression Tests
# ------------------------------------------------------------------------------
tests: all
	@echo "=== Running PF Layer Unit Tests ==="
	@$(MAKE) -C toydb/pflayer tests
	@rm -f file1 file2
	@./toydb/pflayer/testpf > /dev/null && echo "[PASS] testpf (PF buffer & file semantics)"
	@./toydb/pflayer/testhash > /dev/null && echo "[PASS] testhash (PF hash table semantics)"
	@rm -f file1 file2
	@echo "=== Running AM Layer Unit Tests ==="
	@$(MAKE) -C toydb/amlayer a.out misc.o
	@cd toydb/amlayer && $(CC) $(CFLAGS) -o test1 test1.c misc.o amlayer.o ../pflayer/pflayer.o && ./test1 > /dev/null && echo "[PASS] test1 (Multi-type B+ tree index insert & scan)"
	@cd toydb/amlayer && $(CC) $(CFLAGS) -o test2 test2.c misc.o amlayer.o ../pflayer/pflayer.o && ./test2 > /dev/null && echo "[PASS] test2 (100-key sequential B+ tree split & scan)"
	@cd toydb/amlayer && $(CC) $(CFLAGS) -o test3 test3.c misc.o amlayer.o ../pflayer/pflayer.o && ./test3 > /dev/null && echo "[PASS] test3 (Complex interleaved deletions & range scans)"
	@echo "All unit regression tests passed successfully!"

# ------------------------------------------------------------------------------
# Run Benchmarks
# ------------------------------------------------------------------------------
bench: all
	@echo "======================================================================"
	@echo " Running Module 1 Benchmark: PF Layer Buffer & Workload Evaluation"
	@echo "======================================================================"
	@./$(BIN_DIR)/bench_pf
	@echo ""
	@echo "======================================================================"
	@echo " Running Module 2 Benchmark: Slotted Page Space Utilization"
	@echo "======================================================================"
	@./$(BIN_DIR)/bench_slotted
	@echo ""
	@echo "======================================================================"
	@echo " Running Module 3 Benchmark: B+ Tree AM Layer 3-Way Index Evaluation"
	@echo "======================================================================"
	@./$(BIN_DIR)/bench_am

# ------------------------------------------------------------------------------
# Generate Visualization Plots
# ------------------------------------------------------------------------------
plots:
	@echo "Generating visualization plots from benchmark metrics..."
	@python3 scripts/plot_pf_workload.py
	@python3 scripts/plot_space_utilization.py
	@python3 scripts/plot_am_comparison.py
	@echo "All plots successfully created in $(PLOTS_DIR)/"

# ------------------------------------------------------------------------------
# Cleanup
# ------------------------------------------------------------------------------
clean:
	@echo "Cleaning compiled objects and temporary test files..."
	@rm -rf $(BIN_DIR)/*
	@rm -f toydb/slotted/*.o
	@$(MAKE) -C toydb/pflayer clean
	@$(MAKE) -C toydb/amlayer clean
	@rm -f file1 file2 *.sp *.0 *.1 *.idx student_data.sp test_slotted.sp incremental_temp.sp
	@rm -f toydb/amlayer/test1 toydb/amlayer/test2 toydb/amlayer/test3 toydb/amlayer/misc.o toydb/amlayer/*~
	@rm -f toydb/pflayer/file1 toydb/pflayer/file2 toydb/pflayer/a.out
	@echo "Clean completed."

# ------------------------------------------------------------------------------
# Packaging Archive
# ------------------------------------------------------------------------------
pack: clean
	@echo "Packaging project into $(ROLLNO).tar.gz..."
	@tar --exclude='.git' \
	     --exclude='.DS_Store' \
	     --exclude='*.tar.gz' \
	     --exclude='*.tar' \
	     --exclude='*.pdf' \
	     -czvf $(ROLLNO).tar.gz *
	@echo "Archive $(ROLLNO).tar.gz successfully created."
