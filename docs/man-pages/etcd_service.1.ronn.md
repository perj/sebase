etcd_service(1) -- register services in etcd
============================================

## SYNOPSIS

`etcd_service` [ `-key` key ] conffile...


## DESCRIPTION

**etcd_service** reads the services described by the configuration files,
and registers them as platform compatible services in etcd.

The configuration files are bconf files. These are read in the same way
as the SD support in BOS does, and services setup. The difference is that
this program can be used to register services independently from BOS, e.g.
a HTTPD process, or to register that a certain plog state can be queried
on this server.

It's expected that you run etcd in proxy mode somewhere locally and that
proxy then connect to the cluster. Therefore each service only connect
to a single etcd node.


## OPTIONS

* `-key` key:
  Set the root path key of the configuration files, instead of the default
  `sd`. Can be used multiple times to register multiple services, but note
  that it affects all conffiles, so often it's better to add more files
  instead.


## CONFIGURATION

Configuration
=============

Note that the value `$ENV{foo}` can be used to use the `foo` environment variable.
These keys are by default prefixed with the key `sd` to match BOS code. E.g. the
full key for `registry.url` is `sd.registry.url`.

* `registry.url`:
  The URL for etcd. As noted above, it's expected this is a proxy or a single node
  cluster.

* `interval_s=10`:
  How often to check program health and update the information in etcd.
  For backwards compatibility, `healthcheck.interval_s` also works.

* `ttl_s=30`:
  The time to live to set on the etcd directory.

* `service`:
  Service path in etcd. `/service/` will be prefixed.

* `client`:
  Alternative to `service`, you can use the `client` key. In this case `/clients/`
  will be prepended. Also, no healthchecks are done and the "health" key is not
  created. This is an **etcd_service** specific option.

* `healthcheck.url`:
  The url to check health with. Expects a 200 result code if service is working,
  or anything else if it's broken.

* `host.key.source`:
  How to find the host key, one of `value`, `file`, `generate` or `random`.

  `value`:<br>
    Use the static value given with the key below.<br>

  `file`:<br>
    Read from the path given below. The first line is used as key, given
    that it's not empty. Will wait until the file exists and is not empty.

  `generate`:<br>
    Read from the path given, but if it doesn't exist, generate a random
    uuid and write it to the file, which must be writable.
    `/var/tmp` can be useful for this.

  `random`:<br>
    Generate a random uuid each time **etcd_service** is started.

* `host.key.value`:
  The host key to use, if source set to `value`

* `host.key.path`:
  Path to file for source `file` or `generate`.

* `value`:
  This node is copied verbatim to etcd. Used for things that are relatively static.

* `dynval.X.key.Y.value`:
  Keys added to value, each Y is a step in the path. Can be used to make an environment
  variable part of the key. X can be anything, but Y has to be on the form 1, 2, 3 etc.
  
  If a key is missing (empty string) then the value isn't added.

* `dynval.X.value`:
  The value for the matching dynamic key.

## EXAMPLE

Here's a configuration example to register that plog can be queried for the
`foo` service on port 8080, and an auxiliary "controller" port on 8085.
Default cost is 1000, but if you use the host equal to `$ENV{DATACENTER}`,
then cost will be 1.

```
sd.service=foo
sd.healthcheck.url=http://localhost:8085/healthcheck
sd.host.key.value=foo
sd.value.*.*.port=8080
sd.value.*.*.controller_port=8085
sd.value.*.*.cost=1000
sd.dynval.1.key.1.value=$ENV{DATACENTER}
sd.dynval.1.key.2.value=*
sd.dynval.1.key.3.value=cost
sd.dynval.1.value=1
```

## COPYRIGHT

Copyright 2018 Schibsted
