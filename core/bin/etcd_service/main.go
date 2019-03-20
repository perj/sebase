// Copyright 2018 Schibsted

package main

import (
	"context"
	"crypto/rand"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/schibsted/sebase/core/internal/pkg/etcdlight"
	"github.com/schibsted/sebase/vtree/pkg/bconf"
)

const defaultUUIDPath = "/var/tmp/etcd_service-uuid"

type keySlice []string

func (ks *keySlice) Set(value string) error {
	*ks = append(*ks, value)
	return nil
}

func (ks *keySlice) String() string {
	return strings.Join(*ks, ",")
}

func getService(conf bconf.Bconf) (service string, isclient bool) {
	srv := conf.Get("service").String("")
	if srv != "" {
		return "/service/" + srv, false
	}
	srv = conf.Get("client").String("")
	if srv != "" {
		return "/clients/" + srv, true
	}
	log.Fatal("Missing required config value for service")
	return
}

func hostkeyFromFile(path string) string {
	for {
		f, err := os.Open(path)
		if err != nil {
			if os.IsNotExist(err) {
				log.Printf("Waiting for %s to appear", path)
				time.Sleep(500 * time.Millisecond)
				continue
			}
			log.Fatal(err)
		}

		hk := ""
		_, err = fmt.Fscanln(f, &hk)
		if hk != "" {
			return hk
		}

		if err != nil && err != io.EOF {
			log.Fatal(err)
		}
		log.Printf("Waiting for %s to be written", path)
		time.Sleep(500 * time.Millisecond)
	}
}

func hostkeyRandom() string {
	var id [16]byte
	n, err := rand.Read(id[:])
	if err != nil {
		log.Fatal(err)
	}
	if n != len(id) {
		log.Fatal("Short read without error")
	}
	id[6] = (id[6] & 0x0F) | 0x40
	id[8] = (id[8] & 0x3F) | 0x80
	return fmt.Sprintf("%x-%x-%x-%x-%x", id[0:4], id[4:6], id[6:8], id[8:10], id[10:])
}

var generateLock sync.Mutex

func hostkeyGenerateFile(path string) string {
	// Make this function synchronous, in case there are multiple services using the same
	// file path, we don't want them to compete about creating it.
	// Could use an rwlock but it would be more complicated, and this only runs during
	// startup.
	generateLock.Lock()
	defer generateLock.Unlock()

	f, err := os.Open(path)
	if err == nil {
		hk := ""
		_, err = fmt.Fscanln(f, &hk)
		if hk != "" {
			return hk
		}

		if err != nil && err != io.EOF {
			log.Fatal(err)
		}
		err = nil
	} else if !os.IsNotExist(err) {
		log.Fatal(err)
	}

	hk := hostkeyRandom()
	f, err = os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0666)
	if err != nil {
		log.Fatal(err)
	}
	n, err := fmt.Fprintf(f, "%s\n", hk)
	if err != nil {
		log.Fatal(err)
	}
	if n != len(hk)+1 {
		log.Fatal("Short write without error")
	}
	f.Close()
	return hk
}

func getHostkey(conf bconf.Bconf) (hostkey string) {
	switch conf.Get("host.key.source").String("") {
	case "file":
		hostkey = hostkeyFromFile(conf.Get("host.key.path").String(defaultUUIDPath))
	case "value":
	case "":
		hostkey = conf.Get("host.key.value").String("")
	case "generate":
		hostkey = hostkeyGenerateFile(conf.Get("host.key.path").String(defaultUUIDPath))
	case "random":
		hostkey = hostkeyRandom()
	}
	if hostkey == "" {
		log.Fatal("Missing required config value for host key")
	}
	return
}

func buildValues(conf bconf.Bconf) (values map[string]interface{}) {
	values = conf.Get("value").ToMap()
	if values == nil {
		values = make(map[string]interface{})
	}

loop:
	for _, n := range conf.Get("dynval").Slice() {
		kn := n.Get("key")
		if kn.Length() < 1 {
			continue
		}
		v := n.Get("value").String("")
		if v == "" {
			continue
		}

		kl := make([]string, 0, kn.Length())
		for i := 1; i <= kn.Length(); i++ {
			k := kn.Get(fmt.Sprintf("%d.value", i)).String("")
			// If there was an empty key (e.g. empty env var), we skip this value.
			if k == "" {
				continue loop
			}
			kl = append(kl, k)
		}

		dst := values
		var i int
		for i = 0; i < len(kl)-1; i++ {
			switch dst[kl[i]].(type) {
			case map[string]interface{}:
				dst = dst[kl[i]].(map[string]interface{})
			case string:
				log.Fatalf("Node/leaf value conflict at %v", kl[0:i])
			case nil:
				m := make(map[string]interface{})
				dst[kl[i]] = m
				dst = m
			}
		}
		switch dst[kl[i]].(type) {
		case map[string]interface{}:
			log.Fatalf("Node/leaf value conflict at %v", kl)
		case string, nil:
			dst[kl[i]] = v
		}
	}
	return values
}

func addIp(kapi *etcdlight.KAPI, values *map[string]interface{}) {
	// Check if already set. This forces *.* to be a mapping but
	// I think that's reasonable.
	star, ok := (*values)["*"].(map[string]interface{})
	if !ok {
		star = make(map[string]interface{})
		(*values)["*"] = star
	}
	starstar, ok := star["*"].(map[string]interface{})
	if !ok {
		starstar = make(map[string]interface{})
		star["*"] = starstar
	}

	if starstar["name"] != nil {
		return
	}

	ip := ""
	// Intercept the connection and extract the local address.
	// This determines the address we actually use to communicate with.
	t := &http.Transport{
		Dial: func(network, addr string) (net.Conn, error) {
			c, e := net.Dial(network, addr)
			if c != nil {
				addr := c.LocalAddr().String()
				ip, _, _ = net.SplitHostPort(addr)
			}
			return c, e
		},
	}
	c := &http.Client{Transport: t}
	c.Get(kapi.BaseURL.String())

	if ip == "" {
		log.Fatal("Failed to determine local address")
	}

	starstar["name"] = ip
}

func serializeValues(values map[string]interface{}) (confdata string) {
	data, err := json.Marshal(&values)
	if err != nil {
		log.Fatal(err)
	}
	return string(data)
}

func getHealth(conf bconf.Bconf) (health string) {
	url := conf.Get("healthcheck.url").String("")
	if url == "" {
		log.Fatal("No healthcheck url configured")
	}

	resp, err := http.Get(url)
	if err != nil {
		log.Print("Error checking health, assuming service is down. ", err)
		return "down"
	}
	defer resp.Body.Close()

	if resp.StatusCode >= 200 && resp.StatusCode <= 299 {
		log.Printf("Status %d => up", resp.StatusCode)
		return "up"
	}

	log.Printf("Status %d => down", resp.StatusCode)
	return "down"
}

func registerService(kapi *etcdlight.KAPI, ttl time.Duration, service, hostkey, health, prevHealth, config, prevConfig string) {

	dir := service + "/" + hostkey

	newdir := false
	err := kapi.RefreshDir(context.Background(), dir, ttl)
	if etcdlight.IsErrorCode(err, 100) { // Key not found.
		err = kapi.MkDir(context.Background(), dir, ttl)
		newdir = true
	}
	if err != nil {
		log.Print("Error updating directory: ", err)
		newdir = true
	}

	exclusive := false
	if !newdir && config == prevConfig {
		exclusive = true
	}
	err = kapi.Set(context.Background(), dir+"/config", string(config), exclusive, 0)
	if err != nil {
		if etcdlight.IsErrorCode(err, 105) {
			// Means the prevExist check failed, which is ok.
		} else {
			log.Print(err)
		}
	}

	if health == "" {
		// Don't register empty string, which is only possible for static clients.
		return
	}

	newval := newdir || health != prevHealth
	if !newval {
		r, _ := kapi.Get(context.Background(), dir+"/health", false)
		newval = r == nil || len(r.Values) != 1 || r.Values[0].Value != health
	}

	if newval {
		err = kapi.Set(context.Background(), dir+"/health", health, false, 0)
		if err != nil {
			log.Print(err)
		}
	}
}

func unregisterService(kapi *etcdlight.KAPI, service, hostkey string) {

	dir := "/service/" + service + "/" + hostkey

	err := kapi.RmDir(context.Background(), dir, true)
	if err != nil {
		log.Fatal(err)
	}
}

var shutdown chan struct{}

func runService(sdconf bconf.Bconf) {
	rurl := sdconf.Get("registry.url").String("http://127.0.0.1:2379")

	kapi, err := etcdlight.NewKAPI(rurl)
	if err != nil {
		log.Fatal(err)
	}

	values := buildValues(sdconf)
	addIp(kapi, &values)
	confdata := serializeValues(values)

	service, isclient := getService(sdconf)
	hostkey := getHostkey(sdconf)

	prevHealth := ""
	prevConfig := ""

	int_s := sdconf.Get("interval_s").Int(0)
	if int_s == 0 {
		int_s = sdconf.Get("healthcheck.interval_s").Int(10)
	}

	register_interval := time.Duration(int_s) * time.Second
	ttl := time.Duration(sdconf.Get("ttl_s").Int(30)) * time.Second

	running := true
	for running {
		health := ""
		if !isclient {
			health = getHealth(sdconf)
		}

		registerService(kapi, ttl, service, hostkey, health, prevHealth, confdata, prevConfig)

		prevHealth = health
		prevConfig = confdata

		select {
		case <-shutdown:
			running = false
		case <-time.After(register_interval):
		}
	}

	unregisterService(kapi, service, hostkey)
}

func main() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: %s [flags] conffile...\n", os.Args[0])
		flag.PrintDefaults()
	}
	confkeys := make(keySlice, 0)
	flag.Var(&confkeys, "key", "Key to use in conffile. Can be used multiple times to register multiple services.\n"+
		"\tUse empty string for no prefix. Default \"sd\".\n"+
		"\tNote: It's usually better to use multiple files instead of this. Each conffile adds a new service per key.")
	flag.Parse()
	if flag.NArg() < 1 {
		flag.Usage()
		os.Exit(1)
	}

	if len(confkeys) == 0 {
		confkeys = keySlice{"sd"}
	}

	shutdown = make(chan struct{})

	sigch := make(chan os.Signal)
	signal.Notify(sigch, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		<-sigch
		close(shutdown)
	}()

	var work sync.WaitGroup

	one := false
	for _, f := range flag.Args() {
		bc := &bconf.Node{}
		err := bconf.ReadFile(bc, f, true)
		if err != nil {
			log.Fatal(err)
		}

		for _, key := range confkeys {
			var sdconf bconf.Bconf = bc
			if key != "" {
				sdconf = sdconf.Get(key)
			}
			if !sdconf.Valid() {
				log.Printf("File %s key %s not valid, skipping.", f, key)
				continue
			}
			one = true
			work.Add(1)
			go func(sdconf bconf.Bconf) {
				runService(sdconf)
				work.Done()
			}(sdconf)
		}
	}

	if !one {
		os.Exit(1)
	}

	work.Wait()
}
