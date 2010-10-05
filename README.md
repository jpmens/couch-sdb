# Couch-SDB

## Requirements

1. cdbc
2. jansson
3. curl

## Building

1. Read my chapter on Bind's SDB! :)
2. Patch `bin/named/Makefile.in` with

		DBDRIVER_OBJS = couch-sdb.o 
		DBDRIVER_SRCS = couch-sdb.c 
		DBDRIVER_INCLUDES = 
		DBDRIVER_LIBS = -lcdbc -lcurl -ljansson

3. Fix `bin/named/main.c` as per my book.
4. `./configure && make`

## Testing

1. Create a minimal `named.conf` containing something like this:

		controls {
		};

		options {
		    directory "/tmp";
		    listen-on port 9953  { 127.0.0.1; };
		    listen-on-v6 {none;};
		    allow-query {any;};
		};

		zone "example.org" {
		    type master;
		    database "couch http://couch-server.com:5984 dns";
		                ^           ^                    ^^^
		                |           |         	          |
		                |           | 	                  + dbname
		               	|           +---------------------- URL
		               	+---------------------------------- keyword
		};

2. Launch `named` in foreground

		./named -4 -d 1 -g -c jp.conf

3. Query it

		dig -p 9953 @127.0.0.1 www.example.org
