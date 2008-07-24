CXXFLAGS=-Wall -ansi -fno-operator-names -O2 -g -I/usr/local/include\
         -I/usr/include/libxml2 -I/usr/local/include/libxml2
LDLIBS=-lsqlite3 -lxml2 -L/usr/local/lib

SPARQL_OBJECTS=sparql_tokenizer.o sparql_parser.o sparql.o
IMPORT_OBJECTS=turtle_tokenizer.o turtle_parser.o import.o

all: import export query

query: $(SPARQL_OBJECTS)
	$(CXX) $(LDLIBS) -o query $(SPARQL_OBJECTS)

import: $(IMPORT_OBJECTS)
	$(CXX) $(LDLIBS) -o import $(IMPORT_OBJECTS)

clean:
	-rm import
	-rm export
	-rm query
	-rm *.o
