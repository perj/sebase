#!/usr/bin/env bash
# Copyright 2018 Schibsted

exec 2>/dev/null
openssl req -config <(printf "[req]\ndistinguished_name=dname\nprompt=no\n[dname]\nCN=localhost\n") -newkey rsa:2048 -passout pass:temp -new -x509 -days 2 -keyout >(openssl rsa -passin pass:temp)
