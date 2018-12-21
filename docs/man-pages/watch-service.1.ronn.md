watch-service(1) -- query etcd and plog for service state
=========================================================

## SYNOPSIS

`watch-service` [ `-json` ] [ `-host` host ] [ `-appl` appl ] [ `-etcd` url ]<br>
		[ `-noetcd` ] [ `-client` address ] [ `-watch`|`-watch-all` ]<br>
		[ `-nodump` ] [ `-filter` regexp ] service...

## DESCRIPTION

**watch-service** queries etcd and/or fd pool state stored in plog, and displays
the data in a dashboard format. You provide the service or services to query.

Information displayed:

* service node key, often a UUID.
* Host
* Port
* Health: up or down
* Cost for the given host/appl pair.
* Service specific configuration.
* List of clients, with data collected from each client.

Each client has further information displayed, per service node:

  * Number of open connections (active or stored in an fd pool).
  * Peer address used to connect.
  * Cost for this client.

Platform service discovery use the basis of a host and application pair to
configure different clients, for example to set the fd pool cost differently
based on client/server location. **watch-service** will use a wildcard host
by default and the application `mod_blocket` which matches the HTTP webfronts.

**watch-service** also fetches plog locations from etcd, looking for the
service `clients/appl`, e.g. `clients/mod_blocket` by default, and using
the `plog_port` key to find the plog query port. See etcd_service(1) for
how to set this up for e.g. a webfront.

A watch mode is also available, which will wait for updates from etcd and
print them as they happen. In this mode, a new/old key is added before
the values, to display what was changed. A top level key displaying the
event type is also added. Only data present in the event received is displayed.
There's no data from plog in this mode.

## OPTIONS

* `-appl` appl:
  Choose the application name to query state for on clients, and to filter
  the configuration keys with.<br>
  **Default**: `mod_blocket`.

* `-host` host:
  Set the host key used to filter the configuration keys. Can be used to
  find the configuration for a certain set of hosts, instead of the defaults.
  No default.

* `-client` client:
  Add a client plog `host:port` pair to connect to. This is typically the
  plog query port on each webfront to include in the result. Can be used
  multiple times, by default plog is not queried.

* `-etcd` url:
  The URL to use to connect to etcd.<br>
  **Default**: `http://localhost:2379`.

* `-noetcd`:
  Do not connect to etcd. Used to avoid the error message when etcd is not
  available. Off by default since **watch-service** is most useful together
  with etcd.

* `-watch`, `-watch-all`:
  Wait for events from etcd and print the changes. If `-watch-all` is used,
  even events that changed nothing is displayed, otherwise they're filtered
  out.

* `-nodump`:
  Do not display the initiaul full fetch from etcd and plog. Use together with
  `-watch` to only display the events from etcd.

* `-json`:
  Ouput json instead of the default yaml.

* `-filter` regexp:
  Only output data keys matching the regexp. Appies to etcd data keys and to
  client data. E.g. `-filter cost` only outputs the cost keys from etcd and
  plog.

## NOTES

For this to be useful you have to setup at least the fd pools storing state
into plog. That is used by adding the bconf keys `sd.service` or `service`
to bconf for the fd pool nodes in question. See fd_pool(3).

To be really useful, both the service and the clients also should be
registered in etcd. Then all required information is fetched automatically.

## COPYRIGHT

Copyright 2018 Schibsted
