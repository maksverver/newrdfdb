BEGIN;

-- Set page size to UFS2 default fragment size
PRAGMA page_size=2048;

-- Node table and index
-- l: language
-- d: datatype (references Node.rowid)
CREATE TABLE Node (l, d, PRIMARY KEY(l, d));

-- Quad table and indices
-- m: model (references Node.rowid)
-- s: subject (references Node.rowid)
-- p: predicate (references Node.rowid)
-- o: object (references Node.rowid)
CREATE TABLE Quad (m, s, p, o, PRIMARY KEY (m, s, p, o));
CREATE INDEX Quad_spo ON Quad (s, p, o);
CREATE INDEX Quad_po  ON Quad (p, o);
CREATE INDEX Quad_o   ON QUAD (o);

-- Node identifiers for built-in datatypes
INSERT INTO Node (oid, l, d) VALUES ( 0, NULL,           0); -- uri
INSERT INTO Node (oid, l, d) VALUES ( 1, '',             0); -- lexical
INSERT INTO Node (oid, l, d) VALUES ( 2, 'xsd:boolean',  0); -- (integer: 0/1)
INSERT INTO node (oid, l, d) VALUES ( 3, 'xsd:integer',  0); -- (integer)
INSERT INTO Node (oid, l, d) VALUES ( 4, 'xsd:dateTime', 0); -- (integer)
INSERT INTO Node (oid, l, d) VALUES ( 5, 'xsd:float',    0); -- (real)
INSERT INTO Node (oid, l, d) VALUES ( 6, 'xsd:double',   0); -- (real)

-- Increase the default cache size
PRAGMA default_cache_size=32768;

COMMIT;
