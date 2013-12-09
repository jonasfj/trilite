CFLAGS	:= -Ire2/ $(shell pkg-config --cflags sqlite3) -Wall -fPIC -ansi
LDFLAGS := -Lre2/obj -lre2 $(shell pkg-config --libs sqlite3) -shared
SOURCES := kmp.c scanstr.c varint.c hash.c expr.c match.c regexp.cpp cursor.c vtable.c trilite.c
OBJECTS := $(patsubst %.cpp,%.o,$(patsubst %.c,%.o,$(SOURCES))) 
all: debug
debug: CFLAGS += -g
debug: re2 re2/obj/libre2.a libtrilite.so
release: CFLAGS += -DNDEBUG -O3
release: LDFLAGS += -O3
release: re2/obj/libre2.a libtrilite.so
re2:
	hg clone https://re2.googlecode.com/hg re2
re2/obj/libre2.a: re2
	$(MAKE) -C re2 CXXFLAGS='-Wall -O3 -pthread -fPIC'
%.o: %.cpp
	$(CXX) $(CFLAGS) -c $< -o $@
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
libtrilite.so: $(OBJECTS)
	$(CXX) $? $(LDFLAGS) -o $@
check: all
	cat test.sql | sqlite3 -bail; \
	if [ "$$?" -eq "0" ]; then \
		echo "Test passed"; \
	else \
		echo "Test failed!"; \
	fi
clean:
	rm -rf libtrilite.so $(OBJECTS)
dist-clean:
	rm -rf libtrilite.so $(OBJECTS) re2/
