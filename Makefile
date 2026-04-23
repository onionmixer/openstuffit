CC ?= cc
AR ?= ar
RM ?= rm -f
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig
PROJECT_VERSION := 0.1.0
ABI_VERSION := 0
DIST_NAME := openstuffit-0.1.0-m1
DIST_DIR := package/linux
DIST_TARBALL := $(DIST_DIR)/$(DIST_NAME).tar.gz
UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)

BUILD_DIR := build
SRC_DIR := src
TEST_DIR := tests
INCLUDE_DIR := include
EXAMPLE_DIR := examples

COMMON_CFLAGS := -std=c99 -D_FILE_OFFSET_BITS=64 -Wall -Wextra -Wpedantic -Wconversion -fvisibility=hidden
CFLAGS ?= -O2 -g
ALL_CFLAGS := $(COMMON_CFLAGS) $(CFLAGS) -I$(INCLUDE_DIR) -I$(SRC_DIR)
DEPFLAGS := -MMD -MP
LDLIBS ?= -lz

ifeq ($(UNAME_S),Darwin)
SHARED_LIB_REAL_NAME := libopenstuffit.$(PROJECT_VERSION).dylib
SHARED_LIB_ABI_NAME := libopenstuffit.$(ABI_VERSION).dylib
SHARED_LIB_LINK_NAME := libopenstuffit.dylib
SHARED_LDFLAGS := -dynamiclib -Wl,-install_name,$(SHARED_LIB_ABI_NAME)
else
SHARED_LIB_REAL_NAME := libopenstuffit.so.$(PROJECT_VERSION)
SHARED_LIB_ABI_NAME := libopenstuffit.so.$(ABI_VERSION)
SHARED_LIB_LINK_NAME := libopenstuffit.so
SHARED_LDFLAGS := -shared -Wl,-soname,$(SHARED_LIB_ABI_NAME)
endif

LIB_OBJS := \
	$(BUILD_DIR)/ost_archive.o \
	$(BUILD_DIR)/ost_binhex.o \
	$(BUILD_DIR)/ost_crypto.o \
	$(BUILD_DIR)/ost_crc16.o \
	$(BUILD_DIR)/ost_decompress.o \
	$(BUILD_DIR)/ost_detect.o \
	$(BUILD_DIR)/ost_dump.o \
	$(BUILD_DIR)/ost_endian.o \
	$(BUILD_DIR)/ost_extract.o \
	$(BUILD_DIR)/ost_io.o \
	$(BUILD_DIR)/ost_macroman.o \
	$(BUILD_DIR)/ost_sit13.o \
	$(BUILD_DIR)/ost_sit15.o \
	$(BUILD_DIR)/ost_unicode.o

LIB_PIC_OBJS := $(patsubst $(BUILD_DIR)/%.o,$(BUILD_DIR)/pic/%.o,$(LIB_OBJS))
LIB_STATIC := $(BUILD_DIR)/libopenstuffit.a
LIB_SHARED_REAL := $(BUILD_DIR)/$(SHARED_LIB_REAL_NAME)
LIB_SHARED_ABI := $(BUILD_DIR)/$(SHARED_LIB_ABI_NAME)
LIB_SHARED := $(BUILD_DIR)/$(SHARED_LIB_LINK_NAME)
PKG_CONFIG_FILE := $(BUILD_DIR)/openstuffit.pc
CLI_OBJS := $(BUILD_DIR)/openstuffit.o
BRIDGE_OBJS := \
	$(BUILD_DIR)/bridge/openstuffit-fr-bridge.o \
	$(BUILD_DIR)/bridge/openstuffit_fr_bridge_json.o
TEST_OBJS := $(BUILD_DIR)/test_m1.o
TEST_LIB_OBJS := $(BUILD_DIR)/test_library_api.o
EXAMPLE_OBJS := $(BUILD_DIR)/examples/list_archive.o
PUBLIC_HEADERS := $(INCLUDE_DIR)/openstuffit/openstuffit.h
DEPFILES := $(LIB_OBJS:.o=.d) $(LIB_PIC_OBJS:.o=.d) $(CLI_OBJS:.o=.d) $(BRIDGE_OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(TEST_LIB_OBJS:.o=.d) $(EXAMPLE_OBJS:.o=.d)

.PHONY: all lib lib-static lib-shared examples clean test test-unit test-library test-library-static test-library-shared test-examples test-symbols test-install test-pkg-config test-corpus-matrix test-cli test-cli-errors test-password test-fixtures test-list-matrix test-extract-matrix test-fr-bridge test-generated-method-fixtures test-generator-selftest test-json-schema test-json-golden test-error-golden test-path-safety test-large-fixtures test-unicode-filenames test-native-forks test-identify-all test-corrupt test-fuzz-smoke test-docs test-method-scan test-valgrind test-clang test-werror test-cppcheck test-scan-build test-shellcheck build-xadmaster test-xad-compare test-report test-sanitize distcheck distcheck-tarball install uninstall dist format

all: $(BUILD_DIR)/openstuffit $(BUILD_DIR)/openstuffit-fr-bridge lib $(PKG_CONFIG_FILE)

lib: lib-static lib-shared

lib-static: $(LIB_STATIC)

lib-shared: $(LIB_SHARED)

examples: $(BUILD_DIR)/examples/list_archive

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/pic:
	mkdir -p $(BUILD_DIR)/pic

$(BUILD_DIR)/examples:
	mkdir -p $(BUILD_DIR)/examples

$(BUILD_DIR)/openstuffit: $(LIB_OBJS) $(CLI_OBJS) | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD_DIR)/openstuffit-fr-bridge: $(LIB_OBJS) $(BRIDGE_OBJS) | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) $^ -o $@ $(LDLIBS)

$(LIB_STATIC): $(LIB_OBJS) | $(BUILD_DIR)
	$(AR) rcs $@ $^

$(LIB_SHARED_REAL): $(LIB_PIC_OBJS) | $(BUILD_DIR)
	$(CC) $(SHARED_LDFLAGS) $^ -o $@ $(LDLIBS)

$(LIB_SHARED_ABI): $(LIB_SHARED_REAL)
	ln -sf $(notdir $(LIB_SHARED_REAL)) $@

$(LIB_SHARED): $(LIB_SHARED_ABI)
	ln -sf $(notdir $(LIB_SHARED_ABI)) $@

$(PKG_CONFIG_FILE): openstuffit.pc.in | $(BUILD_DIR)
	sed -e 's,@PREFIX@,$(PREFIX),g' \
	    -e 's,@LIBDIR@,$(LIBDIR),g' \
	    -e 's,@INCLUDEDIR@,$(INCLUDEDIR),g' \
	    -e 's,@VERSION@,$(PROJECT_VERSION),g' \
	    $< > $@

$(BUILD_DIR)/test_m1: $(LIB_OBJS) $(TEST_OBJS) | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD_DIR)/test_library_api_static: $(TEST_LIB_OBJS) $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) $(TEST_LIB_OBJS) $(LIB_STATIC) -o $@ $(LDLIBS)

$(BUILD_DIR)/test_library_api_shared: $(TEST_LIB_OBJS) $(LIB_SHARED) | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) $(TEST_LIB_OBJS) -L$(BUILD_DIR) -lopenstuffit -Wl,-rpath,'$$ORIGIN' -o $@ $(LDLIBS)

$(BUILD_DIR)/examples/list_archive: $(BUILD_DIR)/examples/list_archive.o $(LIB_SHARED) | $(BUILD_DIR)/examples
	$(CC) $(ALL_CFLAGS) $(BUILD_DIR)/examples/list_archive.o -L$(BUILD_DIR) -lopenstuffit -Wl,-rpath,'$$ORIGIN/..' -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/pic/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)/pic
	mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -fPIC $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(TEST_DIR)/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/examples/%.o: $(EXAMPLE_DIR)/%.c | $(BUILD_DIR)/examples
	mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) $(DEPFLAGS) -c $< -o $@

test: test-unit test-library test-examples test-symbols test-pkg-config test-corpus-matrix test-cli test-cli-errors test-error-golden test-password test-list-matrix test-extract-matrix test-fr-bridge test-generated-method-fixtures test-generator-selftest test-json-schema test-json-golden test-path-safety test-large-fixtures test-unicode-filenames test-native-forks

test-unit: $(BUILD_DIR)/test_m1
	$(BUILD_DIR)/test_m1

test-library: test-library-static test-library-shared

test-library-static: $(BUILD_DIR)/test_library_api_static
	$(BUILD_DIR)/test_library_api_static

test-library-shared: $(BUILD_DIR)/test_library_api_shared
	$(BUILD_DIR)/test_library_api_shared

test-examples: examples
	$(BUILD_DIR)/examples/list_archive reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit | grep -q 'testfile.txt'

test-symbols: $(LIB_SHARED)
	bash tools/check_exported_symbols.sh $(LIB_SHARED)

test-install: all
	bash tools/run_install_tests.sh

test-pkg-config: $(PKG_CONFIG_FILE)
	bash tools/run_pkg_config_tests.sh

test-corpus-matrix: $(BUILD_DIR)/openstuffit
	bash tools/run_corpus_matrix.sh $(BUILD_DIR)/openstuffit tests/fixtures/corpus_matrix.tsv

test-cli: $(BUILD_DIR)/openstuffit
	$(BUILD_DIR)/openstuffit --version >/dev/null
	$(BUILD_DIR)/openstuffit --help >/dev/null
	$(BUILD_DIR)/openstuffit identify reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit >/dev/null
	$(BUILD_DIR)/openstuffit identify --show-forks reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin >/dev/null
	$(BUILD_DIR)/openstuffit list reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit >/dev/null
	$(BUILD_DIR)/openstuffit list --unicode-normalization nfd reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit >/dev/null
	$(BUILD_DIR)/openstuffit dump --headers reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit >/dev/null
	$(BUILD_DIR)/openstuffit dump --hex 0:4 reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit | grep -q "5349 5421"
	$(BUILD_DIR)/openstuffit dump --json --hex 0:4 reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit | grep -q '"hex":"53495421"'
	$(BUILD_DIR)/openstuffit dump --forks reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin | grep -q "resource fork: present=yes offset=2944 size=25090"
	$(BUILD_DIR)/openstuffit dump --json --forks reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin | grep -q '"wrapper":"macbinary"'
	$(BUILD_DIR)/openstuffit dump --entry 5 reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit | grep -q "path: testfile.txt"
	$(BUILD_DIR)/openstuffit dump --entry testfile.txt reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit | grep -q "data fork: present=yes offset=2792 size=12"
	$(BUILD_DIR)/openstuffit dump --json --entry 5 reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit | grep -q '"path":"testfile.txt"'
	rm -rf /tmp/openstuffit_extract_smoke
	$(BUILD_DIR)/openstuffit extract --overwrite -o /tmp/openstuffit_extract_smoke reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
	test -f "/tmp/openstuffit_extract_smoke/Test Text"
	test -f /tmp/openstuffit_extract_smoke/testfile.txt
	cmp -s /tmp/openstuffit_extract_smoke/testfile.jpg reference_repos/stuffit-test-files/sources/testfile.jpg
	cmp -s /tmp/openstuffit_extract_smoke/testfile.png reference_repos/stuffit-test-files/sources/testfile.png
	cmp -s /tmp/openstuffit_extract_smoke/testfile.PICT reference_repos/stuffit-test-files/sources/testfile.PICT
	test "$$(stat -c %Y /tmp/openstuffit_extract_smoke/testfile.txt)" -eq 1675463846
	rm -rf /tmp/openstuffit_no_preserve_time
	$(BUILD_DIR)/openstuffit extract --no-preserve-time -o /tmp/openstuffit_no_preserve_time reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
	test "$$(stat -c %Y /tmp/openstuffit_no_preserve_time/testfile.txt)" -ne 1675463846
	$(BUILD_DIR)/openstuffit extract --skip-existing -o /tmp/openstuffit_extract_smoke reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
	rm -rf /tmp/openstuffit_rename_existing
	$(BUILD_DIR)/openstuffit extract -o /tmp/openstuffit_rename_existing reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
	$(BUILD_DIR)/openstuffit extract --rename-existing -o /tmp/openstuffit_rename_existing reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
	test -f /tmp/openstuffit_rename_existing/testfile.txt.1
	cp reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit /tmp/openstuffit_crc_bad.sit
	printf X | dd of=/tmp/openstuffit_crc_bad.sit bs=1 seek=564 count=1 conv=notrunc 2>/dev/null
	$(BUILD_DIR)/openstuffit extract --overwrite -o /tmp/openstuffit_crc_bad /tmp/openstuffit_crc_bad.sit; test $$? -eq 3
	$(BUILD_DIR)/openstuffit extract --overwrite --no-verify-crc -o /tmp/openstuffit_crc_bad_noverify /tmp/openstuffit_crc_bad.sit
	rm -rf /tmp/openstuffit_finder_sidecar
	$(BUILD_DIR)/openstuffit extract --overwrite --finder sidecar -o /tmp/openstuffit_finder_sidecar reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
	test -f "/tmp/openstuffit_finder_sidecar/testfile.txt.finder.json"
	grep -q '"type":"TEXT"' "/tmp/openstuffit_finder_sidecar/testfile.txt.finder.json"
	rm -rf /tmp/openstuffit_appledouble
	$(BUILD_DIR)/openstuffit extract --overwrite --forks appledouble -o /tmp/openstuffit_appledouble reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
	test -f "/tmp/openstuffit_appledouble/._testfile.txt"
	test ! -f "/tmp/openstuffit_appledouble/testfile.txt.rsrc"
	rm -rf /tmp/openstuffit_both
	$(BUILD_DIR)/openstuffit extract --overwrite --forks both -o /tmp/openstuffit_both reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
	test -f "/tmp/openstuffit_both/._testfile.txt"
	test -f "/tmp/openstuffit_both/testfile.txt.rsrc"
	dd if="/tmp/openstuffit_both/._testfile.txt" of=/tmp/openstuffit_both_ad_rsrc.bin bs=1 skip=82 2>/dev/null
	cmp -s /tmp/openstuffit_both_ad_rsrc.bin "/tmp/openstuffit_both/testfile.txt.rsrc"

test-cli-errors: $(BUILD_DIR)/openstuffit
	bash tools/run_cli_error_matrix.sh $(BUILD_DIR)/openstuffit

test-error-golden: $(BUILD_DIR)/openstuffit
	bash tools/run_error_golden_tests.sh $(BUILD_DIR)/openstuffit

test-password: $(BUILD_DIR)/openstuffit
	$(BUILD_DIR)/openstuffit list --json reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.password.sit > /tmp/openstuffit_password_classic.json
	grep -q '"format":"sit-classic"' /tmp/openstuffit_password_classic.json
	grep -q '"encrypted":true' /tmp/openstuffit_password_classic.json
	$(BUILD_DIR)/openstuffit extract --overwrite -o /tmp/openstuffit_password_classic reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.password.sit; test $$? -eq 4
	$(BUILD_DIR)/openstuffit extract --password password --overwrite -o /tmp/openstuffit_password_classic_try reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.password.sit; test $$? -eq 2
	$(BUILD_DIR)/openstuffit list --json reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.password.sit.bin > /tmp/openstuffit_password_classic_bin.json
	grep -q '"wrapper":"macbinary"' /tmp/openstuffit_password_classic_bin.json
	grep -q '"compressed_size":272' /tmp/openstuffit_password_classic_bin.json
	$(BUILD_DIR)/openstuffit dump --entry 5 reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.password.sit.bin | grep -q 'data fork: .* encrypted=yes encryption=classic-des classic_padding=4'
	$(BUILD_DIR)/openstuffit dump --json --entry 5 reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.password.sit.bin | grep -q '"encryption":"classic-des","classic_padding":4'
	rm -rf /tmp/openstuffit_password_classic_bin_try
	$(BUILD_DIR)/openstuffit extract --password password --overwrite -o /tmp/openstuffit_password_classic_bin_try reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.password.sit.bin
	cmp -s "/tmp/openstuffit_password_classic_bin_try/Test Text" "reference_repos/stuffit-test-files/sources/Test Text"
	cmp -s /tmp/openstuffit_password_classic_bin_try/testfile.jpg reference_repos/stuffit-test-files/sources/testfile.jpg
	cmp -s /tmp/openstuffit_password_classic_bin_try/testfile.png reference_repos/stuffit-test-files/sources/testfile.png
	cmp -s /tmp/openstuffit_password_classic_bin_try/testfile.PICT reference_repos/stuffit-test-files/sources/testfile.PICT
	cmp -s /tmp/openstuffit_password_classic_bin_try/testfile.txt reference_repos/stuffit-test-files/sources/testfile.txt
	$(BUILD_DIR)/openstuffit extract --password wrong --overwrite -o /tmp/openstuffit_password_classic_bin_bad reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.password.sit.bin; test $$? -eq 4
	$(BUILD_DIR)/openstuffit list --json reference_repos/stuffit-test-files/build/testfile.stuffit7.win.password.sit > /tmp/openstuffit_password_sit5.json
	grep -q '"format":"sit5"' /tmp/openstuffit_password_sit5.json
	grep -q '"encrypted":true' /tmp/openstuffit_password_sit5.json
	$(BUILD_DIR)/openstuffit extract --overwrite -o /tmp/openstuffit_password_sit5 reference_repos/stuffit-test-files/build/testfile.stuffit7.win.password.sit; test $$? -eq 4
	rm -rf /tmp/openstuffit_password_sit5_ok
	$(BUILD_DIR)/openstuffit extract --password password --overwrite -o /tmp/openstuffit_password_sit5_ok reference_repos/stuffit-test-files/build/testfile.stuffit7.win.password.sit
	cmp -s /tmp/openstuffit_password_sit5_ok/sources/testfile.jpg reference_repos/stuffit-test-files/sources/testfile.jpg
	cmp -s /tmp/openstuffit_password_sit5_ok/sources/testfile.png reference_repos/stuffit-test-files/sources/testfile.png
	sha256sum /tmp/openstuffit_password_sit5_ok/sources/testfile.txt | grep -q b2f51cd17b3cbe77f091f887d91110164a2cb5a5a9ebe828c44d655c83dca8eb
	$(BUILD_DIR)/openstuffit extract --password wrong --overwrite -o /tmp/openstuffit_password_sit5_bad reference_repos/stuffit-test-files/build/testfile.stuffit7.win.password.sit; test $$? -eq 4

test-fixtures: $(BUILD_DIR)/openstuffit
	$(BUILD_DIR)/openstuffit identify reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
	$(BUILD_DIR)/openstuffit list -l reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
	$(BUILD_DIR)/openstuffit identify reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea
	$(BUILD_DIR)/openstuffit identify --show-forks reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin
	$(BUILD_DIR)/openstuffit list --json reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin
	$(BUILD_DIR)/openstuffit identify --json --show-forks reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin > /tmp/openstuffit_identify_sea_bin.json
	cmp -s /tmp/openstuffit_identify_sea_bin.json tests/expected/identify_sea_bin.json
	$(BUILD_DIR)/openstuffit list --json reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin > /tmp/openstuffit_list_sea_bin.json
	cmp -s /tmp/openstuffit_list_sea_bin.json tests/expected/list_sea_bin.json
	$(BUILD_DIR)/openstuffit identify reference_repos/stuffit-test-files/build/testfile.stuffit7.win.sit
	$(BUILD_DIR)/openstuffit list -L reference_repos/stuffit-test-files/build/testfile.stuffit7.win.sit
	$(BUILD_DIR)/openstuffit identify --show-forks reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit.hqx
	$(BUILD_DIR)/openstuffit list -L reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit.hqx
	$(BUILD_DIR)/openstuffit identify --show-forks reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.hqx
	cp reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit.hqx /tmp/openstuffit_hqx_crc_bad.hqx
	printf ! | dd of=/tmp/openstuffit_hqx_crc_bad.hqx bs=1 seek=100 count=1 conv=notrunc 2>/dev/null
	$(BUILD_DIR)/openstuffit identify /tmp/openstuffit_hqx_crc_bad.hqx; test $$? -eq 3
	$(BUILD_DIR)/openstuffit identify reference_repos/stuffit-test-files/build/testfile.stuffit7_dlx.mac9.sitx; test $$? -eq 2
	$(BUILD_DIR)/openstuffit identify reference_repos/stuffit-test-files/build/testfile.stuffit7_dlx.mac9.sitx.hqx; test $$? -eq 2
	bash tools/check_extract_manifest.sh $(BUILD_DIR)/openstuffit reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit tests/expected/extract_stuffit45_mac9.tsv
	bash tools/check_extract_manifest.sh $(BUILD_DIR)/openstuffit reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin tests/expected/extract_stuffit45_mac9.tsv
	bash tools/check_extract_manifest.sh $(BUILD_DIR)/openstuffit reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.AS tests/expected/extract_stuffit45_mac9.tsv
	bash tools/check_extract_manifest.sh $(BUILD_DIR)/openstuffit reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit.hqx tests/expected/extract_stuffit45_mac9.tsv
	bash tools/check_extract_manifest.sh $(BUILD_DIR)/openstuffit reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.hqx tests/expected/extract_stuffit45_mac9.tsv
	bash tools/check_extract_manifest.sh $(BUILD_DIR)/openstuffit reference_repos/stuffit-test-files/build/testfile.stuffit7.win.sit tests/expected/extract_stuffit7_win.tsv

test-list-matrix: $(BUILD_DIR)/openstuffit
	bash tools/run_list_matrix.sh $(BUILD_DIR)/openstuffit tests/fixtures/list_matrix.tsv

test-extract-matrix: $(BUILD_DIR)/openstuffit
	bash tools/run_extract_matrix.sh $(BUILD_DIR)/openstuffit tests/fixtures/extract_matrix.tsv

test-fr-bridge: $(BUILD_DIR)/openstuffit-fr-bridge
	bash tests/test_fr_bridge.sh $(BUILD_DIR)/openstuffit-fr-bridge

test-generated-method-fixtures: $(BUILD_DIR)/openstuffit
	bash tools/run_generated_method_fixtures.sh $(BUILD_DIR)/openstuffit

test-generator-selftest:
	python3 tools/gen_sit5_method14_fixture.py selftest

test-json-schema: $(BUILD_DIR)/openstuffit
	bash tools/run_json_schema_tests.sh $(BUILD_DIR)/openstuffit

test-json-golden: $(BUILD_DIR)/openstuffit
	bash tools/run_json_golden_tests.sh $(BUILD_DIR)/openstuffit

test-path-safety: $(BUILD_DIR)/openstuffit
	bash tools/run_path_safety_tests.sh $(BUILD_DIR)/openstuffit

test-large-fixtures: $(BUILD_DIR)/openstuffit
	bash tools/run_large_fixture_tests.sh $(BUILD_DIR)/openstuffit

test-unicode-filenames: $(BUILD_DIR)/openstuffit
	bash tools/run_unicode_filename_tests.sh $(BUILD_DIR)/openstuffit

test-native-forks: $(BUILD_DIR)/openstuffit
	bash tools/run_native_fork_tests.sh $(BUILD_DIR)/openstuffit

test-identify-all: $(BUILD_DIR)/openstuffit
	bash tools/run_identify_all.sh $(BUILD_DIR)/openstuffit reference_repos/stuffit-test-files/build

test-corrupt: $(BUILD_DIR)/openstuffit
	bash tools/run_corrupt_tests.sh $(BUILD_DIR)/openstuffit

test-fuzz-smoke: $(BUILD_DIR)/openstuffit
	bash tools/run_fuzz_smoke.sh $(BUILD_DIR)/openstuffit

test-docs:
	bash tools/check_spec_docs.sh

test-method-scan: $(BUILD_DIR)/openstuffit
	bash tools/scan_methods.sh $(BUILD_DIR)/openstuffit tests/reports/method_scan_latest.md

test-valgrind: $(BUILD_DIR)/openstuffit $(BUILD_DIR)/test_m1
	bash tools/run_valgrind_smoke.sh $(BUILD_DIR)/openstuffit $(BUILD_DIR)/test_m1

build-xadmaster:
	bash tools/build_xadmaster_clang.sh reference_repos/XADMaster

test-xad-compare: $(BUILD_DIR)/openstuffit
	bash tools/compare_xadmaster.sh $(BUILD_DIR)/openstuffit tests/reports/xad_compare_latest.md

test-report: $(BUILD_DIR)/openstuffit
	bash tools/write_test_report.sh $(BUILD_DIR)/openstuffit tests/reports/latest.md

test-sanitize:
	$(MAKE) clean
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) test CFLAGS="-O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer"

test-clang:
	$(MAKE) clean
	$(MAKE) test CC=clang

test-werror:
	$(MAKE) clean
	$(MAKE) test CFLAGS="-O2 -g -Werror"

test-cppcheck:
	cppcheck --enable=warning,style,performance,portability --error-exitcode=1 --std=c99 --inline-suppr --suppress=missingIncludeSystem -Isrc src tests

test-scan-build:
	@if command -v scan-build >/dev/null 2>&1; then scan-build --status-bugs $(MAKE) clean all; else echo "scan-build not found; skipping"; fi

test-shellcheck:
	@if command -v shellcheck >/dev/null 2>&1; then shellcheck tools/*.sh; else echo "shellcheck not found; skipping"; fi

distcheck:
	$(MAKE) clean
	$(MAKE) all
	$(MAKE) test
	$(MAKE) test-corrupt
	$(MAKE) test-fuzz-smoke
	$(MAKE) test-docs
	$(MAKE) test-report

distcheck-tarball: dist
	bash tools/run_distcheck_tarball.sh "$(DIST_TARBALL)"

install: $(BUILD_DIR)/openstuffit $(BUILD_DIR)/openstuffit-fr-bridge $(LIB_STATIC) $(LIB_SHARED)
	install -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(LIBDIR)" "$(DESTDIR)$(INCLUDEDIR)/openstuffit" "$(DESTDIR)$(PKGCONFIGDIR)"
	install -m 0755 $(BUILD_DIR)/openstuffit "$(DESTDIR)$(BINDIR)/openstuffit"
	install -m 0755 $(BUILD_DIR)/openstuffit-fr-bridge "$(DESTDIR)$(BINDIR)/openstuffit-fr-bridge"
	install -m 0644 $(LIB_STATIC) "$(DESTDIR)$(LIBDIR)/libopenstuffit.a"
	install -m 0755 $(LIB_SHARED_REAL) "$(DESTDIR)$(LIBDIR)/$(SHARED_LIB_REAL_NAME)"
	ln -sf $(SHARED_LIB_REAL_NAME) "$(DESTDIR)$(LIBDIR)/$(SHARED_LIB_ABI_NAME)"
	ln -sf $(SHARED_LIB_ABI_NAME) "$(DESTDIR)$(LIBDIR)/$(SHARED_LIB_LINK_NAME)"
	install -m 0644 $(PUBLIC_HEADERS) "$(DESTDIR)$(INCLUDEDIR)/openstuffit/"
	sed -e 's,@PREFIX@,$(PREFIX),g' \
	    -e 's,@LIBDIR@,$(LIBDIR),g' \
	    -e 's,@INCLUDEDIR@,$(INCLUDEDIR),g' \
	    -e 's,@VERSION@,$(PROJECT_VERSION),g' \
	    openstuffit.pc.in > "$(DESTDIR)$(PKGCONFIGDIR)/openstuffit.pc"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/openstuffit"
	rm -f "$(DESTDIR)$(BINDIR)/openstuffit-fr-bridge"
	rm -f "$(DESTDIR)$(LIBDIR)/libopenstuffit.a"
	rm -f "$(DESTDIR)$(LIBDIR)/$(SHARED_LIB_LINK_NAME)"
	rm -f "$(DESTDIR)$(LIBDIR)/$(SHARED_LIB_ABI_NAME)"
	rm -f "$(DESTDIR)$(LIBDIR)/$(SHARED_LIB_REAL_NAME)"
	rm -f "$(DESTDIR)$(PKGCONFIGDIR)/openstuffit.pc"
	rm -f "$(DESTDIR)$(INCLUDEDIR)/openstuffit/openstuffit.h"
	-rmdir "$(DESTDIR)$(INCLUDEDIR)/openstuffit" 2>/dev/null || true
	-rmdir "$(DESTDIR)$(PKGCONFIGDIR)" 2>/dev/null || true

dist:
	rm -rf build/dist "$(DIST_DIR)"
	mkdir -p build/dist/"$(DIST_NAME)" "$(DIST_DIR)"
	cp -R Makefile README.md LICENSE openstuffit.pc.in include src tests tools document examples PLAN_DEV_OPENSTUFFIT.md build/dist/"$(DIST_NAME)"/
	mkdir -p build/dist/"$(DIST_NAME)"/reference_repos
	cp -R reference_repos/stuffit-test-files build/dist/"$(DIST_NAME)"/reference_repos/
	cp -R reference_repos/sit build/dist/"$(DIST_NAME)"/reference_repos/
	tar -C build/dist -czf "$(DIST_TARBALL)" "$(DIST_NAME)"

format:
	@if command -v clang-format >/dev/null 2>&1; then clang-format -i $(SRC_DIR)/*.[ch] $(TEST_DIR)/*.[ch]; else echo "clang-format not found"; fi

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPFILES)
