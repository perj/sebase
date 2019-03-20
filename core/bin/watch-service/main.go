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
	"net/url"
	"os"
	"path"
	"regexp"
	"strings"

	"github.com/schibsted/sebase/core/internal/pkg/etcdlight"
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

	kapi, err := etcdClient(etcdUrl)
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
	watchers := make(map[string]etcdlight.Watcher)

	for _, service := range flag.Args() {
		var kvs []etcdlight.KV
		if !noEtcd {
			r, err := kapi.Get(context.Background(), "/service/"+service, true)
			if err != nil {
				log.Print(err)
			} else {
				kvs = r.Values
			}
			if watch {
				watchers[service] = kapi.Watch("/service/"+service, true, 0)
			}
		}
		addService(result, service, kvs, allpools)
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

func etcdClient(burl string) (*etcdlight.KAPI, error) {
	baseurl, err := url.Parse(burl)
	if err != nil {
		return nil, err
	}
	tport := *http.DefaultTransport.(*http.Transport)
	tport.TLSClientConfig = &tls.Config{}
	if cafile != "" {
		cadata, err := ioutil.ReadFile(cafile)
		if err != nil {
			return nil, err
		}
		capool := x509.NewCertPool()
		if !capool.AppendCertsFromPEM(cadata) {
			return nil, fmt.Errorf("No CA certs found in ca file")
		}
		tport.TLSClientConfig.RootCAs = capool
	}
	if certfile != "" {
		certdata, err := ioutil.ReadFile(certfile)
		if err != nil {
			return nil, err
		}
		cert, err := tls.X509KeyPair(certdata, certdata)
		if err != nil {
			return nil, err
		}
		tport.TLSClientConfig.Certificates = append(tport.TLSClientConfig.Certificates, cert)
	}
	kapi := &etcdlight.KAPI{
		Client: &http.Client{
			Transport: &tport,
		},
		BaseURL: baseurl,
	}
	return kapi, nil
}

func etcdClients(kapi *etcdlight.KAPI, appl string) clientSlice {
	r, err := kapi.Get(context.Background(), "/clients/"+appl, true)
	if err != nil {
		if !etcdlight.IsErrorCode(err, 100) { // Key not found
			log.Print(err)
		}
		return nil
	}

	res := make(clientSlice, 0)

	nodes := make(map[string]interface{})
	addConfHealth(nodes, r.Values, true)
	for _, nv := range nodes {
		conf, _ := nv.(map[string]interface{})["data"].(map[string]interface{})
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

func addService(result map[string]interface{}, service string, kvs []etcdlight.KV, allpools map[string]map[string]interface{}) {
	data := make(map[string]interface{})
	result[service] = data

	pnodes, nks := filterPlogNodes(service, allpools)

	addConfHealth(data, kvs, false)
	for nk := range data {
		addClients(data, nk, pnodes)
		delete(nks, nk)
	}

	// Check for nodes found in plog but not in etcd.
	for nk := range nks {
		ndata := make(map[string]interface{})
		data[nk] = ndata
		addClients(ndata, nk, pnodes)
	}
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

func addConfHealth(result map[string]interface{}, kvs []etcdlight.KV, nofilter bool) {
	for _, n := range kvs {
		nkey := path.Base(path.Dir(n.Key))
		m, _ := result[nkey].(map[string]interface{})
		if m == nil {
			m = make(map[string]interface{})
			result[nkey] = m
		}
		data, _ := m["data"].(map[string]interface{})
		if data == nil {
			data = make(map[string]interface{})
			m["data"] = data
		}
		switch path.Base(n.Key) {
		case "config":
			addConf(data, n.Value, nofilter)
		case "health":
			addCheckFiltered(data, "health", n.Value, nofilter)
		default:
			log.Print("unexpected key in etcd: ", n.Key)
		}
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
