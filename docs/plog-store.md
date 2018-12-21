# Plog-store

Plog-store is a tool is for aggregating log lines into log objects.
An example of an object would be a transaction call including all input, output and log lines.
The whole object is then sent off to logstash, which passes it on.

Since it keeps track of sessions it will also know if one was interrupted abnormally, and
can then mark the object as incomplete.

It will also keep a "log state" for each client application, where stuff such as number of
connections, database size etc. can be kept. Those are sent off at client shutdown.

## COPYRIGHT

Copyright 2018 Schibsted
