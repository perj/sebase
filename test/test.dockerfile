# Copyright 2018 Schibsted

FROM ubuntu:xenial

ENV PATH=/root/go/bin:/usr/local/go/bin:$PATH

RUN apt-get update && apt-get install -y curl etcd g++ gcc git gperf jq libbsd-dev libcurl4-openssl-dev libicu-dev libpcre3-dev libssl-dev libyajl-dev make ninja-build protobuf-c-compiler python && apt-get clean
RUN curl https://dl.google.com/go/go1.14.2.linux-amd64.tar.gz | tar -C /usr/local -xzf -
RUN GO111MODULE=on go get github.com/schibsted/sebuild/v2/cmd/seb
# Python is only needed for test, should try to remove it maybe.

# Trick etcd into sending systemd notifications like we want.
RUN mkdir -p /run/systemd/system
