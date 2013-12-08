select load_extension('./bin/libtrilite.so');
create virtual table trg using trilite (identifier key, name text, image blob);
