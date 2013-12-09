select load_extension('./libtrilite.so');
create virtual table trg using trilite;
-- TriLite current creates two underlying tables
.tables
-- Wrap insertions into a transaction (not strictly required)
BEGIN TRANSACTION;
insert into trg (id, text) VALUES (1, 'abcd');
insert into trg (id, text) VALUES (2, 'bcde');
insert into trg (id, text) VALUES (3, 'bcdef');
insert into trg (id, text) VALUES (4, 'abc');
insert into trg (id, text) VALUES (4, 'abCd');
-- Note that we can only query data after we've committed
-- This is just a detail left out so far.
COMMIT TRANSACTION;
-- See we have content
select * from trg_content;
-- We also have doclists, Note the trigram is encoded as integer
select trigram, length(doclist), hex(doclist) from trg_index;
-- Now, let's select something, -extents: tells, to generate extents when matching
select *, hex(extents(contents)) from trg WHERE contents MATCH 'substr-extents:abc' and contents MATCH 'regexp-extents:bcd';
-- Okay let's try isubstr
select "Testing isubstr:";
select * from trg WHERE contents MATCH 'isubstr-extents:aBc';
;
