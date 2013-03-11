CFLAGS	:= -Ire2/ $(shell pkg-config --cflags sqlite3) -Wall -fPIC -ansi
LDFLAGS := -Lre2/obj -lre2 $(shell pkg-config --libs sqlite3) -shared
SOURCES :=  cursor.c hash.c pattern.c trilite.c varint.c vtable.c
OBJECTS := $(patsubst %.cpp,obj/%.o,$(patsubst %.c,obj/%.o,$(SOURCES))) 
all: debug
debug: CFLAGS += -g
debug: re2 bin/libtrilite.so
release: CFLAGS += -DNDEBUG -O3 -flto
release: LDFLAGS += -flto -O3
release: re2 bin/libtrilite.so
re2:
	hg clone https://re2.googlecode.com/hg re2
	$(MAKE) -C re2 CXXFLAGS='-Wall -O3 -pthread -fPIC'
obj/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) -c $< -o $@
obj/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
bin/libtrilite.so: $(OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $? $(LDFLAGS) -o $@
clean:
	rm -rf bin/ obj/
dist-clean:
	rm -rf bin/ obj/ re2/
