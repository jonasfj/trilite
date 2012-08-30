TriLite, An Inverted Trigram Index for Accelerated String Matching in Sqlite
============================================================================
TriLite is an **experimental** Sqlite extension for creating an inverted
trigram index to accelerate string matching queries, such as regular
expressions and substring matching.

A trigram is essential any substring of length 3, the inverted index maps from 
trigrams, to a list of document ids for documents containing the given trigram.
When querying for a substring, trigrams is extracted from the substring, and
documents id lists for the trigrams is fetched and merged, after which only
documents containing all the trigrams are matched against the substring and
returned if considered a result.

The approach is pretty much
[How Google Code Search Worked](http://swtch.com/~rsc/regexp/regexp4.html),
and similar to the [pg_trgm](http://www.postgresql.org/docs/9.1/static/pgtrgm.html)
module for PostgreSQL. See related work for more information.

License
=======
TriLite sources is unless otherwise noted released into public domain.


Current State
=============
TriLite is under active development and currently supports the bare minimum to
make it usable for static databases, ie. `INSERT` and accelerated substring and
regular expression matching.

TriLite still has a lot of short comings, and this code is a work in progress.
To sum this up do **NOT** use TriLite in a production environment, unless you
are me. Currently TriLite can do a few tricks as outlined in `test.sql`,
which you can pipe to `sqlite3` after building TriLite.


Things To Do
============
   - Add reference counted regular expression -> compiled expression cache to vtable
   - Add rowid, column -> first match cache to regular expression cache entries
   - Support multiple columns of different types
      - Alias rowid, with PRIMARY INTEGER KEY
      - Auxiliary non-indexed columns (useful for storing extents and file id)
      - Indexed text columns (useful for tname and tqualname)
   - Handle extents for expressions with the empty string in the language
   - Limit number of extents returned per file by queries
   - Facilitate update/delete
   - Use a hash set to optimize extraction of unique trigrams
   - Optimize temporary hash table
   - Query documents inserted in the same transaction, ie. before commit as currently required
   - Table specific options, configurable at runtime not compile time
      - Forbid full table scan with MATCH using regular expressions
      - Max pending insert cache (bytes)
      - Max pending insert list
      - Max pending delete cache (bytes)
      - Max pending delete list
   - Handle LIKE/GLOB patterns (ie. case (in)sensitive ordered substrings)
   - Write Documentation


Credits
=======
TriLite would not been as fast without these awesome projects.

  * [re2](http://code.google.com/p/re2) fast regular expression engine.
  * [Railgun_Doublet](http://www.sanmayce.com/Railgun/index.html) fast
    strstr-like substring matching.

Related Work
============
Other projects with similar aim,

  * [How Google Code Search Worked](http://swtch.com/~rsc/regexp/regexp4.html)
    in particular [the standalone engine](https://code.google.com/p/codesearch/)
  * [Index support for regular expression search](http://www.pgcon.org/2012/schedule/events/383.en.html)
    presentation at PostgreSQL conference 2012.
  * [pg_trgm](http://www.postgresql.org/docs/9.1/static/pgtrgm.html) trigram
    module for PostgreSQL, accelerating `LIKE` queries.

