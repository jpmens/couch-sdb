# Couch-SDB


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
				    |           | 		      |
				    |           | 		      + dbname
				    |           +---------------------- URL
				    +---------------------------------- keyword
		};




