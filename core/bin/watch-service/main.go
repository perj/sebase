// Copyright 2018 Schibsted

package main

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"net"
	"net/http"
	"os"
	"path"
	"regexp"
	"strings"
	"time"

	"github.com/coreos/etcd/client"
	yaml "gopkg.in/yaml.v2"
)

type clientSlice []string

var (
	host     = "*"
	appl     = "mod_blocket"
	noEtcd   = false
	etcdUrl  = "http://localhost:2379"
	cafile   = ""
	certfile = ""
	clients  = make(clientSlice, 0)
	ojson    = false
	watch    = false
	watchall = false
	nodump   = false
	filter   = ""
	filterRE *regexp.Regexp
)

func (cs *clientSlice) Set(value string) error {
	*cs = append(*cs, value)
	return nil
}

func (cs *clientSlice) String() string {
	return strings.Join(*cs, ",")
}

func main() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: %s [flags] service...\n", os.Args[0])
		flag.PrintDefaults()
	}
	flag.StringVar(&host, "host", host, "Bconf host (for sd value)")
	flag.StringVar(&appl, "appl", appl, "Appl to use for bconf and plog state queries.")
	flag.StringVar(&etcdUrl, "etcd", etcdUrl, "Base URL for etcd")
	flag.StringVar(&cafile, "cafile", cafile, "File containing CA certificate")
	flag.StringVar(&certfile, "certfile", certfile, "File containing client certificate")
	flag.BoolVar(&noEtcd, "noetcd", noEtcd, "Disable talking to etcd.")
	flag.BoolVar(&ojson, "json", ojson, "Output json instead of yaml.")
	flag.Var(&clients, "client", "Client host:port to query plog on. Can be used multiple times.")
	flag.BoolVar(&watch, "watch", watch, "Watch for etcd events, only changes are printed")
	flag.BoolVar(&watchall, "watch-all", watchall, "Watch for etcd events, all events are printed")
	flag.BoolVar(&nodump, "nodump", nodump, "Don't print the initial fetch of all data. Use together with -watch")
	flag.StringVar(&filter, "filter", filter, "Only output keys that match this regexp.")
	flag.Parse()

	if filter != "" {
		var err error
		filterRE, err = regexp.Compile(filter)
		if err != nil {
			log.Fatal(err)
		}
	}

	if flag.NArg() < 1 {
		flag.Usage()
		os.Exit(1)
	}

	if watchall {
		watch = true
	}

	_, kapi, err := etcdClient(etcdUrl)
	if err != nil && !noEtcd {
		log.Fatal(err)
	}

	if !noEtcd && appl != "" {
		ecls := etcdClients(kapi, appl)
		clients = append(clients, ecls...)
	}

	allpools := make(map[string]map[string]interface{})
	for _, client := range clients {
		host, _, err := net.SplitHostPort(client)
		if err != nil {
			log.Print(err)
			continue
		}
		pools, err := fetchPlog("http://"+client, appl)
		if err != nil {
			log.Print(err)
			continue
		}
		allpools[host] = pools
	}

	result := make(map[string]interface{})
	watchers := make(map[string]client.Watcher)

	for _, service := range flag.Args() {
		var node *client.Node
		if !noEtcd {
			resp, err := kapi.Get(context.Background(), "/service/"+service, &client.GetOptions{Recursive: true})
			if err != nil {
				log.Print(err)
			} else {
				node = resp.Node
			}
			if watch {
				watchers[service] = kapi.Watcher("/service/"+service, &client.WatcherOptions{Recursive: true})
			}
		}
		addService(result, service, node, allpools)
	}

	if !nodump {
		output(result)
	}

	if len(watchers) != 0 {
		watchEtcd(watchers)
	}
}

func output(result interface{}) {
	var data []byte
	var err error
	if ojson {
		data, err = json.MarshalIndent(result, "", "\t")
		data = append(data, '\n')
	} else {
		data, err = yaml.Marshal(result)
	}
	if err != nil {
		log.Fatal(err)
	}
	if !ojson {
		os.Stdout.WriteString("---\n")
	}
	os.Stdout.Write(data)
}

func etcdClient(url string) (client.Client, client.KeysAPI, error) {
	tport := *client.DefaultTransport.(*http.Transport)
	tport.TLSClientConfig = &tls.Config{}
	if cafile != "" {
		cadata, err := ioutil.ReadFile(cafile)
		if err != nil {
			return nil, nil, err
		}
		capool := x509.NewCertPool()
		if !capool.AppendCertsFromPEM(cadata) {
			return nil, nil, fmt.Errorf("No CA certs found in ca file")
		}
		tport.TLSClientConfig.RootCAs = capool
	}
	if certfile != "" {
		certdata, err := ioutil.ReadFile(certfile)
		if err != nil {
			return nil, nil, err
		}
		cert, err := tls.X509KeyPair(certdata, certdata)
		if err != nil {
			return nil, nil, err
		}
		tport.TLSClientConfig.Certificates = append(tport.TLSClientConfig.Certificates, cert)
	}
	cfg := client.Config{
		Endpoints: []string{url},
		Transport: &tport,
		// set timeout per request to fail fast when the target endpoint is unavailable
		HeaderTimeoutPerRequest: 3 * time.Second,
	}
	c, err := client.New(cfg)
	if err != nil {
		return nil, nil, err
	}
	kapi := client.NewKeysAPI(c)
	return c, kapi, nil
}

func etcdClients(kapi client.KeysAPI, appl string) clientSlice {
	resp, err := kapi.Get(context.Background(), "/clients/"+appl, &client.GetOptions{Recursive: true})
	if err != nil {
		if !client.IsKeyNotFound(err) {
			log.Print(err)
		}
		return nil
	}

	res := make(clientSlice, 0)

	for _, n := range resp.Node.Nodes {
		data := make(map[string]interface{})
		addConfHealth(data, n, true)
		conf, _ := data["data"].(map[string]interface{})
		if conf == nil {
			continue
		}
		host, _ := conf["host"].(string)
		plogPort, _ := conf["plog_port"].(string)
		if host != "" && plogPort != "" {
			res = append(res, net.JoinHostPort(host, plogPort))
		}
	}
	return res
}

// Might return nil, nil if no fd pools found.
func fetchPlog(baseurl, app string) (map[string]interface{}, error) {
	resp, err := http.Get(baseurl + "/state/" + app)
	if err != nil {
		return nil, err
	}
	switch resp.StatusCode {
	case 200:
		break
	case 404:
		return nil, errors.New("app not found")
	default:
		return nil, errors.New("unknown HTTP error " + string(resp.StatusCode))
	}
	var v struct {
		Value struct {
			Fd_pools map[string]interface{}
		}
	}
	data, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}
	resp.Body.Close()
	err = json.Unmarshal(data, &v)
	if err != nil {
		return nil, err
	}
	return v.Value.Fd_pools, nil
}

func filterPlogNodes(service string, allpools map[string]map[string]interface{}) (map[string][]map[string]interface{}, map[string]bool) {
	// host => slice of node data dict.
	pnodes := make(map[string][]map[string]interface{})

	nks := make(map[string]bool)

	for _, p := range allpools {
		si, ok := p["services"]
		if !ok {
			continue
		}
		services := si.(map[string]interface{})
		serv := services[service].(map[string]interface{})
		for skey := range serv {
			for pkey := range serv[skey].(map[string]interface{}) {
				portmap := p["ports"].(map[string]interface{})[pkey].(map[string]interface{})
				/*
					if portmap["connections"] == nil || portmap["connections"].(float64) == 0 {
						continue
					}
				*/
				portmap["key"] = skey
				nks[skey] = true
				pnodes[pkey] = append(pnodes[skey], portmap)
			}
		}
	}
	return pnodes, nks
}

func addService(result map[string]interface{}, service string, node *client.Node, allpools map[string]map[string]interface{}) {
	data := make(map[string]interface{})
	result[service] = data

	pnodes, nks := filterPlogNodes(service, allpools)

	if node != nil {
		for _, n := range node.Nodes {
			nk := addServiceNode(data, n, pnodes)
			delete(nks, nk)
		}
	}

	// Check for nodes found in plog but not in etcd.
	for nk := range nks {
		ndata := make(map[string]interface{})
		data[nk] = ndata
		addClients(ndata, nk, pnodes)
	}
}

func addServiceNode(result map[string]interface{}, node *client.Node, pnodes map[string][]map[string]interface{}) string {
	nodekey := path.Base(node.Key)

	data := make(map[string]interface{})
	result[nodekey] = data
	addConfHealth(data, node, false)
	addClients(data, nodekey, pnodes)
	return nodekey
}

func addClients(result map[string]interface{}, nodekey string, pnodes map[string][]map[string]interface{}) {
	var data map[string]interface{}
	for k, p := range pnodes {
		for _, pn := range p {
			if pn["key"] == nodekey {
				if data == nil {
					data = make(map[string]interface{})
				}
				ndata := make(map[string]interface{})
				data[k] = ndata
				conns := pn["connections"]
				if conns == nil {
					conns = 0
				}
				addCheckFiltered(ndata, "connections", conns, false)
				addCheckFiltered(ndata, "cost", pn["cost"], false)
				addCheckFiltered(ndata, "peer", pn["peer"], false)
			}
		}
	}
	if data != nil {
		result["clients"] = data
	}
}

func addConfHealth(result map[string]interface{}, node *client.Node, nofilter bool) {
	var conf, health *client.Node
	for _, n := range node.Nodes {
		switch path.Base(n.Key) {
		case "config":
			conf = n
		case "health":
			health = n
		default:
			log.Print("unexpected key in etcd: ", n.Key)
		}
	}
	data := make(map[string]interface{})
	if conf != nil {
		addConf(data, conf.Value, nofilter)
	}
	if health != nil {
		addCheckFiltered(data, "health", health.Value, nofilter)
	}
	if len(data) > 0 {
		result["data"] = data
	}
}

func addConf(result map[string]interface{}, value string, nofilter bool) {
	var v map[string]map[string]interface{}
	err := json.Unmarshal([]byte(value), &v)
	if err == nil {
		m := mergeConf(v, host, appl)
		addCheckFiltered(result, "host", m["name"], nofilter)
		delete(m, "name")
		for k, v := range m {
			addCheckFiltered(result, k, v, nofilter)
		}
	} else {
		result["confdata_error"] = err.Error()
	}
}

func addCheckFiltered(result map[string]interface{}, key string, value interface{}, nofilter bool) {
	if !nofilter && filterRE != nil && !filterRE.MatchString(key) {
		return
	}
	result[key] = value
}

func mergeConf(v map[string]map[string]interface{}, host, appl string) map[string]interface{} {
	topstar := v["*"]
	tophost := v[host]
	var starstar, starappl, hoststar, hostappl map[string]interface{}
	if topstar != nil {
		starstar, _ = topstar["*"].(map[string]interface{})
		starappl, _ = topstar[appl].(map[string]interface{})
	}
	if tophost != nil {
		hoststar, _ = tophost["*"].(map[string]interface{})
	}
	if starstar == nil {
		starstar = make(map[string]interface{})
	}
	for _, m := range []map[string]interface{}{starappl, hoststar, hostappl} {
		for k, v := range m {
			// XXX: assumes merging happens at this level instead of further down.
			starstar[k] = v
		}
	}
	return starstar
}
