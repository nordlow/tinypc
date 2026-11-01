TOP = ../..
include $(TOP)/Makefile
SRC = $(TOPSRC)/tests/tests2
VPATH = $(SRC)

TESTS = $(patsubst %.c,%.test,\
    $(sort $(notdir $(wildcard $(SRC)/??_*.c)))\
    $(sort $(notdir $(wildcard $(SRC)/???_*.c))))

# The compiler accepts .d as a C source extension for D-style file
# names, so test it outside the *.c wildcard above.
TESTS += 383_d_source_extension.test

# some tests do not pass on all platforms, remove them for now
SKIP = 34_array_assignment.test # array assignment is not in C standard
ifeq (-$(CONFIG_arm_eabi)-$(CONFIG_arm_vfp)-,-yes--)
 SKIP += 22_floating_point.test
endif
ifeq (,$(filter i386,$(ARCH)))
 SKIP += 98_al_ax_extend.test 99_fastcall.test
endif
ifeq (,$(filter i386 x86_64,$(ARCH)))
 SKIP += 85_asm-outside-function.test # x86 asm
 SKIP += 127_asm_goto.test    # hardcodes x86 asm
endif
ifeq ($(CONFIG_backtrace),no)
 SKIP += 113_btdll.test
 CONFIG_bcheck = no
# no bcheck without backtrace
endif
ifeq ($(CONFIG_bcheck),no)
 SKIP += 112_backtrace.test
 SKIP += 114_bound_signal.test
 SKIP += 115_bound_setjmp.test
 SKIP += 116_bound_setjmp2.test
 SKIP += 117_builtins.test
 SKIP += 126_bound_global.test
 SKIP += 132_bound_test.test
 SKIP += 319_d_slice_expr_bcheck.test
endif
ifeq ($(CONFIG_dll),no)
 SKIP += 113_btdll.test # no shared lib support yet
 SKIP += 148_linker_symbols.test
endif
ifeq (-$(findstring gcc,$(CC))-,--)
 SKIP += $(patsubst %.expect,%.test,$(GEN-ALWAYS))
endif
ifeq (-$(CONFIG_WIN32)-$(CONFIG_i386)$(CONFIG_arm)-,--yes-)
 SKIP += 95_bitfields%.test # type_align is different on 32bit-non-windows
endif
ifeq (-$(CONFIG_WIN32)-,-yes-)
 SKIP += 106_versym.test # No pthread support
 SKIP += 114_bound_signal.test # No pthread support
 SKIP += 117_builtins.test # win32 port doesn't define __builtins
 SKIP += 148_linker_symbols.test # ELF linker tests
endif
ifneq (,$(filter OpenBSD FreeBSD NetBSD DragonFly,$(TARGETOS)))
 SKIP += 106_versym.test # no pthread_condattr_setpshared
 SKIP += 114_bound_signal.test # libc problem signal/fork
 SKIP += 116_bound_setjmp2.test # No TLS_FUNC/TLS_VAR in bcheck.c
 SKIP += 144_tls.test 146_tls_extern.test # No tls support
endif
ifeq ($(CONFIG_OSX),yes)
 SKIP += 144_tls.test 146_tls_extern.test # TLS runtime not supported on Mach-O
 SKIP += 148_linker_symbols.test # ELF linker tests
endif
ifeq ($(CONFIG_pie),yes)
 SKIP += 148_linker_symbols.test # PIE executables do not use copy relocations
endif
ifeq (-$(ARCH)-,-arm-)
 SKIP += 144_tls.test 146_tls_extern.test # doesn't work currently
endif
ifneq (-$(ARCH)-,-arm64-)
 SKIP += 138_arm64_encoding.test
 SKIP += 139_arm64_errors.test
 SKIP += 140_arm64_extasm.test
endif
ifneq (-$(ARCH)-,-riscv64-)
 SKIP += 141_riscv_asm.test # riscv64 asm
endif
ifneq (-$(ARCH)-$(CONFIG_WIN32)-,-arm64-yes-)
 SKIP += 145_winarm64_interlocked.test
endif

# Some tests might need arguments
ARGS =
31_args.test : ARGS = arg1 arg2 arg3 arg4 arg5
46_grep.test : ARGS = '[^* ]*[:a:d: ]+\:\*-/: $$' $(SRC)/46_grep.c

# And some tests don't test the right thing with -run
NORUN =
42_function_pointer.test : NORUN = true
# riscv64 asm tests validate encoding, raw regs may crash at runtime

# Some tests might need different flags
FLAGS =
76_dollars_in_identifiers.test : FLAGS += -fdollars-in-identifiers
ifneq (-$(CONFIG_WIN32)-,-yes-)
22_floating_point.test: FLAGS += -lm
24_math_library.test: FLAGS += -lm
endif

# These tests run several snippets from the same file one by one
60_errors_and_warnings.test : FLAGS += -dt
96_nodata_wanted.test : FLAGS += -dt
139_arm64_errors.test : FLAGS += -dt
307_static_assert_errors.test : FLAGS += -dt
318_d_slice_expr_error.test : FLAGS += -dt
319_d_slice_expr_bcheck.test : FLAGS += -b
321_d_foreach_error.test : FLAGS += -dt
322_d_foreach_bcheck.test : FLAGS += -b
324_d_dollar_error.test : FLAGS += -dt
325_d_dollar_bcheck.test : FLAGS += -b
326_d_typedecl.test : GEN = $(GEN-TCC)
327_d_typedecl_error.test : FLAGS += -dt
328_d_typedecl_bcheck.test : FLAGS += -b
329_d_string_slice.test : GEN = $(GEN-TCC)
330_d_string_slice_error.test : FLAGS += -dt
331_d_string_slice_bcheck.test : FLAGS += -b
332_d_darray.test : GEN = $(GEN-TCC)
333_d_darray_error.test : FLAGS += -dt
334_d_darray_bcheck.test : FLAGS += -b
335_d_unittest.test : FLAGS += -unittest
335_d_unittest.test : GEN = $(GEN-TCC)
386_d_immutable_string_literal.test : FLAGS += -unittest
386_d_immutable_string_literal.test : GEN = $(GEN-TCC)
336_d_unittest_off.test : GEN = $(GEN-TCC)
337_d_unittest_error.test : FLAGS += -dt
338_d_immutable.test : GEN = $(GEN-TCC)
339_d_immutable_error.test : FLAGS += -dt
340_debug_dumps.test : FLAGS += -fdump-types -fdump-decls -fdump-vstack -dt
340_debug_dumps.test : GEN = $(GEN-TCC)
341_test_coverage_warn.test : FLAGS += -ftest-coverage -Wuncovered
341_test_coverage_warn.test : NORUN = true
341_test_coverage_warn.test : GEN = $(GEN-TCC)
341_test_coverage_warn.test : FILTER = 2>&1 | sed -e 's,.*/,,g'
345_test_coverage_warn_multiple.test : FLAGS += -ftest-coverage -Wuncovered
345_test_coverage_warn_multiple.test : NORUN = true
345_test_coverage_warn_multiple.test : GEN = $(GEN-TCC)
345_test_coverage_warn_multiple.test : FILTER = 2>&1 | sed -e 's,.*/,,g'
346_test_coverage_warn_disabled.test : FLAGS += -ftest-coverage -Wuncovered -Wno-uncovered
346_test_coverage_warn_disabled.test : NORUN = true
346_test_coverage_warn_disabled.test : GEN = $(GEN-TCC)
347_test_coverage_warn_without_coverage.test : FLAGS += -Wuncovered
347_test_coverage_warn_without_coverage.test : NORUN = true
347_test_coverage_warn_without_coverage.test : GEN = $(GEN-TCC)
348_test_coverage_warn_error.test : FLAGS += -ftest-coverage -Werror=uncovered
348_test_coverage_warn_error.test : NORUN = true
348_test_coverage_warn_error.test : GEN = $(GEN-TCC)
348_test_coverage_warn_error.test : FILTER = 2>&1 | sed -e 's,.*/,,g'
352_d_slice_assign_mismatch_copy.test : FLAGS += -fslice-len-mismatch=copy
353_d_slice_assign_mismatch_ignore.test : FLAGS += -fslice-len-mismatch=ignore
354_d_slice_assign_error.test : FLAGS += -dt
355_d_slice_op_assign.test : FLAGS += -lang=d
356_d_slice_op_assign_error.test : FLAGS += -dt -lang=d
357_d_slice_assign_mismatch_abort_flag.test : FLAGS += -fslice-len-mismatch=abort
358_d_scope_exit.test : FLAGS += -lang=d
358_d_scope_exit.test : GEN = $(GEN-TCC)
360_implicit_run.test : T1 = cd $(SRC) && $(abspath $(TCC_LOCAL)) -B$(abspath $(TOP)) 360_implicit_run one two
361_implicit_compile.test : T1 = cd $(SRC) && $(abspath $(TCC_LOCAL)) -B$(abspath $(TOP)) -c 361_implicit_compile -o $(CURDIR)/361_implicit_compile.o && rm -f $(CURDIR)/361_implicit_compile.o
362_d_slice_assign_empty_brackets.test : FLAGS += --lang=d
362_d_slice_assign_empty_brackets.test : GEN = $(GEN-TCC)
363_d_slice_assign_empty_brackets_mismatch.test : FLAGS += -lang=d
363_d_slice_assign_empty_brackets_mismatch.test : GEN = $(GEN-TCC)
364_d_slice_assign_empty_brackets_error.test : FLAGS += -dt -lang=d
365_c_slice_empty_brackets_error.test : FLAGS += -dt
366_d_assert_alias.test : FLAGS += -lang=d
367_d_version_debug.test : FLAGS += -lang=d
367_d_version_debug.test : GEN = $(GEN-TCC)
368_d_version_debug_error.test : FLAGS += -dt -lang=d
370_d_version_debug_options.test : FLAGS += -lang=d -version=CmdVersion -version=2 -version=none -debug -debug=CmdDebug -debug=2
370_d_version_debug_options.test : GEN = $(GEN-TCC)
371_d_version_debug_unittest.test : FLAGS += -lang=d -unittest
371_d_version_debug_unittest.test : GEN = $(GEN-TCC)
372_d_sizeof_property.test : FLAGS += -lang=d
372_d_sizeof_property.test : GEN = $(GEN-TCC)
373_scope_guards_error.test : FLAGS += -dt -lang=d
373_scope_guards_error.test : GEN = $(GEN-TCC)
374_d_assert.test : FLAGS += -lang=d
374_d_assert.test : GEN = $(GEN-TCC)
381_d_safety_pointer_arith.test : FLAGS += -dt -lang=d
381_d_safety_pointer_arith.test : GEN = $(GEN-TCC)
383_d_source_extension.test : FLAGS += -lang=d
384_d_assert_cmp.test : FLAGS += -lang=d
384_d_assert_cmp.test : GEN = $(GEN-TCC)
385_d_assert_cmp_ne.test : FLAGS += -lang=d
385_d_assert_cmp_ne.test : GEN = $(GEN-TCC)
386_d_assert_cmp_arr.test : FLAGS += -lang=d
386_d_assert_cmp_arr.test : GEN = $(GEN-TCC)
387_d_assert_cmp_arr_stacked.test : FLAGS += -lang=d
387_d_assert_cmp_arr_stacked.test : GEN = $(GEN-TCC)
388_d_assert_cmp_arr_len_mismatch.test : FLAGS += -lang=d
388_d_assert_cmp_arr_len_mismatch.test : GEN = $(GEN-TCC)
389_d_assert_cmp_double.test : FLAGS += -lang=d
389_d_assert_cmp_double.test : GEN = $(GEN-TCC)
390_d_assert_cmp_float.test : FLAGS += -lang=d
390_d_assert_cmp_float.test : GEN = $(GEN-TCC)
391_d_assert_cmp_signed.test : FLAGS += -lang=d
391_d_assert_cmp_signed.test : GEN = $(GEN-TCC)
392_d_assert_cmp_unsigned.test : FLAGS += -lang=d
392_d_assert_cmp_unsigned.test : GEN = $(GEN-TCC)
393_d_assert_cmp_char.test : FLAGS += -lang=d
393_d_assert_cmp_char.test : GEN = $(GEN-TCC)
394_d_assert_cmp_bool.test : FLAGS += -lang=d
394_d_assert_cmp_bool.test : GEN = $(GEN-TCC)
395_d_assert_cmp_pointer.test : FLAGS += -lang=d
395_d_assert_cmp_pointer.test : GEN = $(GEN-TCC)
396_d_assert_cmp_long_double.test : FLAGS += -lang=d
396_d_assert_cmp_long_double.test : GEN = $(GEN-TCC)
397_d_assert_cmp_scalars_pass.test : FLAGS += -lang=d
397_d_assert_cmp_scalars_pass.test : GEN = $(GEN-TCC)
398_scope_attributes.test : FLAGS += -dt -lang=d
398_scope_attributes.test : GEN = $(GEN-TCC)
401_d_pure_global_access.test : FLAGS += -dt -lang=d
401_d_pure_global_access.test : GEN = $(GEN-TCC)
402_d_statement_attributes.test : FLAGS += -dt -lang=d
402_d_statement_attributes.test : GEN = $(GEN-TCC)
403_d_purity_inference.test : FLAGS += -dt -lang=d
403_d_purity_inference.test : GEN = $(GEN-TCC)
404_d_safety_inference.test : FLAGS += -dt -lang=d
404_d_safety_inference.test : GEN = $(GEN-TCC)
407_unused_warnings.test : FLAGS += -Wunused
407_unused_warnings.test : NORUN = true
407_unused_warnings.test : T1 = $(TCC) $(FLAGS) -c $1 2>&1
407_unused_warnings.test : FILTER = | sed -e 's,.*/,,g'
408_d_scoped_enum.test : FLAGS += -lang=d
408_d_scoped_enum.test : GEN = $(GEN-TCC)
409_d_nocopy_instance.test : FLAGS += -lang=d
409_d_nocopy_instance.test : GEN = $(GEN-TCC)
410_d_nocopy_instance_error.test : FLAGS += -dt -lang=d
410_d_nocopy_instance_error.test : GEN = $(GEN-TCC)
411_d_diagnostics_column.test : FLAGS += -dt -lang=d -fdiagnostics-show-column
411_d_diagnostics_column.test : GEN = $(GEN-TCC)
412_d_final_variable.test : FLAGS += -lang=d
412_d_final_variable.test : GEN = $(GEN-TCC)
413_d_final_variable_error.test : FLAGS += -dt -lang=d
413_d_final_variable_error.test : GEN = $(GEN-TCC)
414_d_stack_assignment_error.test : FLAGS += -dt -lang=d
414_d_stack_assignment_error.test : GEN = $(GEN-TCC)
415_d_return_local_error.test : FLAGS += -dt -lang=d
415_d_return_local_error.test : GEN = $(GEN-TCC)
416_d_stack_address_false_positives.test : FLAGS += -lang=d
416_d_stack_address_false_positives.test : GEN = $(GEN-TCC)
418_auto_assignment_operator.test : GEN = $(GEN-TCC)
419_auto_assignment_operator_error.test : FLAGS += -dt
419_auto_assignment_operator_error.test : GEN = $(GEN-TCC)
421_tuple_type.test : GEN = $(GEN-TCC)
422_tuple_type_error.test : FLAGS += -dt
424_d_tuple_comma_compat.test : FLAGS += -lang=d
424_d_tuple_comma_compat.test : GEN = $(GEN-TCC)
425_tuple_abi.test : FLAGS += $(subst 425,425+,$1)
425_tuple_abi.test : GEN = $(GEN-TCC)
425_tuple_abi.test : NORUN = true
426_tuple_abi_bcheck.test : FLAGS += -b
426_tuple_abi_bcheck.test : GEN = $(GEN-TCC)
427_tuple_c_dollar.test : GEN = $(GEN-TCC)
417_d_foreach_qualifiers.test : FLAGS += -lang=d
417_d_foreach_qualifiers.test : GEN = $(GEN-TCC)
104_inline.test : FLAGS += $(subst 104,104+,$1)
104_inline.test : GEN = $(GEN-TCC)


# this test needs two files, and we want to invoke the linker
120_alias.test : FLAGS += $(subst 120,120+,$1)
120_alias.test : GEN = $(GEN-TCC)
120_alias.test : NORUN = true

# this test needs pthread
106_versym.test: FLAGS += -pthread
106_versym.test: NORUN = true

# constructor/destructor
108_constructor.test: NORUN = true

112_backtrace.test: FLAGS += -dt -b
112_backtrace.test 113_btdll.test 126_bound_global.test: FILTER += \
    -e 's;[0-9A-Fa-fx]\{5,\};........;g' \
    -e 's;0x[0-9A-Fa-f]\{1,\};0x?;g'

# this test creates two DLLs and an EXE
113_btdll.test: T1 = \
	mkdir -p .tmp && \
	$(TCC) -bt $1 -shared -D DLL=1 -o .tmp/a1$(DLLSUF) && \
	$(TCC) -bt $1 -shared -D DLL=2 -o .tmp/a2$(DLLSUF) && \
	$(TCC) -bt $1 .tmp/a1$(DLLSUF) .tmp/a2$(DLLSUF) -Wl,-rpath=.tmp -o .tmp/$*.exe && \
	./.tmp/$*.exe

114_bound_signal.test: FLAGS += -b
114_bound_signal.test: NORUN = true # tcc -run does not support fork and -b and SELINUX
115_bound_setjmp.test: FLAGS += -b
116_bound_setjmp2.test: FLAGS += -b
117_builtins.test: T1 = ( $(TCC) -run $1 && $(TCC) -b -run $1 )
ifneq ($(CONFIG_bcheck),no)
121_struct_return.test: FLAGS += -b
122_vla_reuse.test: FLAGS += -b
endif
125_atomic_misc.test: FLAGS += -dt
124_atomic_counter.test: FLAGS += -pthread
126_bound_global.test: FLAGS += -b
126_bound_global.test: NORUN = true
128_run_atexit.test: FLAGS += -dt
132_bound_test.test: FLAGS += -b
140_arm64_extasm.test: GEN = $(GEN-TCC)
141_riscv_asm.test: GEN = $(GEN-TCC)

# TLS needs executable and pthread, not -run
144_tls.test: FLAGS += -pthread
144_tls.test: NORUN = true

# Verify that undefined TLS symbols in TCC objects work with the system linker.
# (note: '$*' is '146_tls_extern' here)
146_tls_extern.test: T1 = \
	mkdir -p .tmp && \
	$(TCC) -c $1 -o .tmp/$*-main.o && \
	$(TCC) -c $1 -DDEFS -o .tmp/$*-defs.o && \
	$(CC) -no-pie -Wl,-z,noexecstack .tmp/$*-main.o .tmp/$*-defs.o -o .tmp/$*.exe && \
	./.tmp/$*.exe
ifeq (-$(CONFIG_WIN32)-,-yes-)
146_tls_extern.test : CC=$(TCC)
endif

# Check ELF linker symbols
148_linker_symbols.test: T1 = \
	mkdir -p .tmp && \
	for T in 1 2 3 4 5 6; do \
	  echo; echo "--- TEST $$T ---"; F=.tmp/$*-$$T; \
	  $(TCC) $1 -DTEST=$$T+100 -c -fcommon -o $$F.o && \
	  $(TCC) $1 -DTEST=$$T+200 -shared -o $$F.so -fPIC && \
	  $(TCC) $1 -DTEST=$$T -Wl,-rpath=.tmp -o $$F.exe $$F.o $$F.so -no-pie && \
	  ./$$F.exe; \
	done
148_linker_symbols.test: GEN=$(T1)

# Filter source directory in warnings/errors (out-of-tree builds)
FILTER = 2>&1 | sed -e 's,$(SRC)/,,g'

all test tests2.all: $(filter-out $(SKIP),$(TESTS))
	@$(MAKE) clean --no-print-directory -s

%.test: %.c %.expect
	@echo Test: $*...
	@mkdir -p .tmp
	@$(call T1,$<) $(FILTER) >.tmp/$*.output 2>&1 || true
	@diff -Nbu $(filter %.expect,$^) .tmp/$*.output
	@rm -f .tmp/$*.output $(filter %.expect,$(GEN-ALWAYS))

%.test: %.d %.expect
	@echo Test: $*...
	@mkdir -p .tmp
	@$(call T1,$<) $(FILTER) >.tmp/$*.output 2>&1 || true
	@diff -Nbu $(filter %.expect,$^) .tmp/$*.output
	@rm -f .tmp/$*.output $(filter %.expect,$(GEN-ALWAYS))

T1 = $(TCC) $(FLAGS) $(T2) $(ARGS)
T2 = $(if $(NORUN),$1 -o .tmp/$*.exe && ./.tmp/$*.exe,-run $1)

# run single test and update .expect file, e.g. "make tests2.37+"
tests2.%+:
	@$(MAKE) $(call F2,$(call F1,$*)) --no-print-directory

# just run tcc to see the output, e.g. "make tests2.37-"
tests2.%-:
	@$(MAKE) $(call F1,$*) T3= --no-print-directory

# run single test, e.g. "make tests2.37"
tests2.%:
	@$(MAKE) $(call F1,$*) --no-print-directory

F1 = $(or $(filter $1_%,$(TESTS)),$1_???.test)
F2 = $1 UPDATE="$(patsubst %.test,%.expect,$1)"

# automatically generate .expect files with gcc:
%.expect :
	@echo Generating: $@
	@$(call GEN,$(SRC)/$*.c) $(FILTER) >$@ 2>&1
	@rm -f *.exe *.obj *.pdb

# using TCC for .expect if -dt in FLAGS
GEN = $(if $(filter -dt -bt -b,$(FLAGS)),$(GEN-TCC),$(GEN-CC))
GEN-CC = @mkdir -p .tmp && $(CC) -w -std=gnu99 $(FLAGS) $1 -o .tmp/$*.exe && ./.tmp/$*.exe $(ARGS)
GEN-TCC = $(T1)
GEN-MSC = $(MS-CC) $1 && ./$*.exe
MS-CC = cl

# tell make not to delete
.PRECIOUS: %.expect

# force .expect generation for these files
$(sort $(GEN-ALWAYS) $(UPDATE)) : force
force:

clean :
	rm -rf .tmp fred.txt $(GEN-ALWAYS)
