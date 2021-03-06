# Builds the HTTP server
HTTPD_SRC := $(wildcard ./httpd/*.c)
HTTPD_HDR := $(wildcard ./httpd/*.h)
HTTPD_OBJ := $(HTTPD_SRC:%.c=%.o)
HTTPD_OBJ := $(HTTPD_OBJ:./httpd/%=./.httpd/httpd.d/%)
HTTPD_DEP := $(HTTPD_OBJ:%.o:%.d)
HTTPD_FLAGS := -fms-extensions -pthread
MMTEST_FLAGS := -DMM_TEST $(HTTPD_FLAGS) -Wno-write-strings -pthread
PATEST_FLAGS := -DPALLOC_TEST $(HTTPD_FLAGS)
CACHETEST_FLAGS := -DCACHE_TEST $(HTTPD_FLAGS)

-include $(HTTPD_DEP)

all: .httpd/httpd
.httpd/httpd: $(HTTPD_OBJ)
	gcc -g -pthread -static $(HTTPD_FLAGS) $(CFLAGS) -o "$@" $^

.httpd/httpd.d/%.o : httpd/%.c $(HTTPD_HDR)
	mkdir -p `dirname $@`
	gcc -g -pthread -c -o $@ $(HTTPD_FLAGS) $(CFLAGS) -MD -MP -MF ${@:.o=.d} $<

# Builds a test harness for the mm_alloc code
all: .httpd/test_mm
.httpd/test_mm: .httpd/test_mm.d/mm_alloc.o .httpd/test_mm.d/test_mm.o
	gcc -g -static $(MMTEST_FLAGS) $(CFLAGS) -o "$@" $^ -lcunit

.httpd/test_mm.d/%.o: httpd/%.c $(HTTPD_HDR)
	mkdir -p `dirname $@`
	gcc -g -c -o $@ $(MMTEST_FLAGS) $(CFLAGS) -MD -MP -MF ${@:.o=.d} $<

check: .httpd/test_mm.cunit_out

all: .httpd/test_cache
.httpd/test_cache: .httpd/test_cache.d/mm_alloc.o .httpd/test_cache.d/palloc.o .httpd/test_cache.d/cache.o .httpd/test_cache.d/test_cache.o
	g++ -O2 -static -Wall -pthread -Werror -std=c++11 $(CACHETEST_FLAGS) $(CXXFLAGS) \
					-o $@ $^ -lcunit
.httpd/test_cache.d/%.o: httpd/%.c $(HTTPD_HDR)
	mkdir -p `dirname $@`
	gcc -g -c -pthread -o $@ $(CACHETEST_FLAGS) $(CFLAGS) -MD -MP -MF ${@:.o=.d} $<

check: .httpd/test_cache.cunit_out

all: .httpd/test_fuzz
.httpd/test_fuzz: .httpd/test_mm.d/mm_alloc.o ./httpd/test_fuzz.cc
	g++ -O2 -static -Wall -Werror -std=c++11 $(MMTEST_FLAGS) $(CXXFLAGS) \
					-o $@ $^ -lcunit

check: .httpd/test_fuzz.cunit_out

# Builds a test harness for the palloc code
all: .httpd/test_palloc
.httpd/test_palloc: .httpd/test_palloc.d/palloc.o \
				.httpd/test_palloc.d/mm_alloc.o \
				.httpd/test_palloc.d/test_palloc.o
	gcc -g -pthread -static $(PATEST_FLAGS) $(CFLAGS) -o "$@" $^ -lcunit

.httpd/test_palloc.d/%.o: httpd/%.c $(HTTPD_HDR)
	mkdir -p `dirname $@`
	gcc -g -c -pthread -o $@ $(PATEST_FLAGS) $(CFLAGS) -MD -MP -MF ${@:.o=.d} $<

check: .httpd/test_palloc.cunit_out

all: .httpd/test_prctl
.httpd/test_prctl: .httpd/test_prctl.d/test_prctl.o
	gcc -g -static -DPRCTL_TEST $(CFLAGS) -o "$@" $^ -lcunit
.httpd/test_prctl.d/test_prctl.o: httpd/test_prctl.c
	mkdir -p `dirname $@`
	gcc -g -c -o $@ -DPRCTL_TEST $(CFLAGS) httpd/test_prctl.c -static -lcunit
	touch lab0/interactive_config
