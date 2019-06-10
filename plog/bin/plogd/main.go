// Copyright 2018 Schibsted

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"os/signal"
	"runtime"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/schibsted/sebase/plog/internal/pkg/plogproto"
)

func handleOpenContext(sessionStore *SessionStorage, dataStore *DataStorage, parentSess *Session, info *plogproto.OpenContext) (*Session, error) {
	var output SessionOutput
	var err error
	var stype plogproto.CtxType
	if len(info.Key) == 0 {
		return nil, fmt.Errorf("No context key given.")
	}
	if info.Ctxtype == nil {
		info.Ctxtype = new(plogproto.CtxType)
	}
	switch *info.Ctxtype {
	case plogproto.CtxType_log, plogproto.CtxType_state, plogproto.CtxType_count:
		if parentSess != nil {
			return nil, fmt.Errorf("Parent context id not allowed for this context type.")
		}
		output, err = dataStore.findOutput(info.Key[0], *info.Ctxtype)
		stype = *info.Ctxtype
		/* Allow direct open of a sub dictionary */
		for _, step := range info.Key[1:] {
			if err == nil {
				var noutput SessionOutput
				noutput, err = output.OpenDict(step)
				output.Close(true, false)
				output = noutput
			}
		}
	case plogproto.CtxType_dict, plogproto.CtxType_list:
		if parentSess == nil {
			return nil, fmt.Errorf("Parent context id is required for this context type.")
		}
		switch *info.Ctxtype {
		case plogproto.CtxType_list:
			output, err = parentSess.Writer.OpenList(info.Key[0])
		case plogproto.CtxType_dict:
			output, err = parentSess.Writer.OpenDict(info.Key[0])
		}
		stype = parentSess.SessionType
	}
	if err != nil {
		return nil, err
	}
	if output == nil {
		return nil, fmt.Errorf("Assertion output != nil failed")
	}

	if stype == plogproto.CtxType_count {
		output = NewCountOutput(output)
	}

	return sessionStore.newSession(output, stype)
}

func handlePublishMessage(sess *Session, publish *plogproto.PlogMessage) error {
	key := ""
	if publish.Key != nil {
		key = *publish.Key
	}

	if sess.SessionType == plogproto.CtxType_log {
		// For log types we can skip the decode as nothing parses it.
		return sess.Writer.Write(key, json.RawMessage(publish.Value))
	}
	var v interface{}
	if err := json.Unmarshal(publish.Value, &v); err != nil {
		return err
	}
	return sess.Writer.Write(key, v)
}

func handleConnection(sessionStore *SessionStorage, dataStore *DataStorage, r *plogproto.Reader) {
	sessCache := make(map[uint64]*Session)
	var msg plogproto.Plog
	for {
		err := r.Receive(&msg)
		if err != nil {
			if err != io.EOF {
				log.Print("Aborted connection: ", err)
			}
			break
		}
		if msg.CtxId == nil || *msg.CtxId == 0 {
			log.Print("Missing context id.")
			break
		}
		var sess *Session
		if msg.Open != nil {
			if sessCache[*msg.CtxId] != nil {
				log.Printf("Duplicate session open id %d", msg.CtxId)
				break
			}
			var psess *Session
			if msg.Open.ParentCtxId != nil && *msg.Open.ParentCtxId > 0 {
				psess = sessCache[*msg.Open.ParentCtxId]
				if psess == nil {
					log.Printf("Missing parent session for context id.")
					break
				}
			}
			sess, err = handleOpenContext(sessionStore, dataStore, psess, msg.Open)
			if err != nil {
				log.Printf("Aborted connection: %s", err)
				break
			}
			sessCache[*msg.CtxId] = sess
		}
		if sess == nil {
			sess = sessCache[*msg.CtxId]
			if sess == nil {
				log.Printf("Session missing.")
				break
			}
		}
		for _, msg := range msg.Msg {
			err := handlePublishMessage(sess, msg)
			if err != nil {
				log.Printf("Aborted connection: %s", err)
				break
			}
		}
		if msg.Close != nil && *msg.Close {
			sess.Close(true)
			delete(sessCache, *msg.CtxId)
		}
	}
	for _, sess := range sessCache {
		sess.Close(false)
	}
}

var Work sync.WaitGroup
var sigCh = make(chan os.Signal)
var quitDoneCh = make(chan struct{})

func listen(sessionStore *SessionStorage, dataStore *DataStorage, l net.Listener, seqpacket bool) {
	pl := plogproto.Listener{l, seqpacket}
	for {
		conn, err := pl.Accept()
		if err != nil {
			log.Print(err)
			break
		}

		Work.Add(1)
		go func() {
			handleConnection(sessionStore, dataStore, conn)
			conn.Close()
			Work.Done()
		}()
	}
}

func listenUnix(netw, path string) (net.Listener, error) {
	if st, err := os.Lstat(path); err == nil {
		if st.Mode()&os.ModeSocket != 0 {
			os.Remove(path)
		}
	}
	l, err := net.Listen(netw, path)
	if err == nil {
		os.Chmod(path, 0666)
	}
	return l, err
}

func systemdMessage(message string) error {
	id := os.Getenv("NOTIFY_SOCKET")
	if id == "" {
		return nil
	}
	c, err := net.DialUnix("unixgram", nil, &net.UnixAddr{id, "unixgram"})
	if err == nil {
		_, err = c.Write([]byte(message))
	}
	return err
}

func filterOutput(out StorageOutput, subprog string) StorageOutput {
	if subprog != "" {
		out = &subprogFilter{out, strings.Split(subprog, ",")}
	}
	return out
}

func main() {
	logstashAddr := flag.String("json", os.Getenv("PLOG_JSON_ADDR"), "Connect to this logstash json compatible tcp address (host:port)")
	jsonFile := flag.String("file", os.Getenv("PLOG_JSON_FILE"), "Write logs to this file, one json dictionary per line.")
	sock := os.Getenv("PLOG_UNIX_SOCKET")
	if sock == "" {
		sock = plogproto.DefaultSock
	}
	sockPath := flag.String("unix-socket", sock, "Listen to this unix socket (config file takes precedence). "+
		"If file name is either plog.sock or plog-packet.sock, both will be created as unix/unixpacket versions (not on OS X where unixpacket is not supported).")
	sockPathNet := flag.String("unix-socktype", "", "Explicitly set the socket type for --unix-socket. "+
		"Disables the automatic detection and creation of a second socket. Valid values: \"unix\", \"unixpacket\" (OS X does not support unixpacket).")
	httpAddr := flag.String("httpd", os.Getenv("PLOG_HTTPD_ADDR"), "Run HTTP server on this address")
	pidfile := flag.String("pidfile", "", "Write PID to this file. Truncated early but the pid is written once the service is ready to accept answers")
	subprog := flag.String("subprog", "", "If set, split the prog field on + and add the second value to the output with this key.")

	if os.Getenv("GOMAXPROCS") == "" && strings.HasPrefix(runtime.Version(), "go1.4") {
		runtime.GOMAXPROCS(20)
	}

	flag.Parse()

	var pidF *os.File
	if *pidfile != "" {
		var err error
		pidF, err = os.OpenFile(*pidfile, os.O_WRONLY|os.O_TRUNC|os.O_CREATE, 0666)
		if err != nil {
			log.Fatal("Failed to open pidfile: ", err)
		}
	}

	sessionStore := SessionStorage{}
	dataStore := DataStorage{}

	if *httpAddr == "" {
		*httpAddr = ":8180"
	}
	s := &server{dataStore: &dataStore}

	var err error
	switch {
	case *logstashAddr != "" && *jsonFile != "":
		err = fmt.Errorf("Only one of -json and -file can be used.")
	case *logstashAddr != "":
		dataStore.Output, err = NewNetWriter("tcp", *logstashAddr)
	case *jsonFile != "":
		w, err := NewJsonFileWriter(*jsonFile)
		if err == nil {
			dataStore.Output = w
			s.fileWriter = w
			defer w.Close()
		}
	default:
		dataStore.Output = jsonIoWriter{os.Stdout}
	}
	if err != nil {
		log.Fatal(err)
	}
	dataStore.Output = filterOutput(dataStore.Output, *subprog)

	s.run(*httpAddr)

	defPacket := false
	var addrs [][2]string
	if *sockPathNet != "" {
		addrs = [][2]string{{*sockPathNet, *sockPath}}
	} else if *sockPath == "plog.sock" || strings.HasSuffix(*sockPath, "/plog.sock") {
		defPacket = true
		addrs = [][2]string{
			{"unix", *sockPath},
			{"unixpacket", strings.TrimSuffix(*sockPath, "plog.sock") + "plog-packet.sock"},
		}
	} else if *sockPath == "plog-packet.sock" || strings.HasSuffix(*sockPath, "/plog-packet.sock") {
		addrs = [][2]string{
			{"unixpacket", *sockPath},
			{"unix", strings.TrimSuffix(*sockPath, "plog-packet.sock") + "plog.sock"},
		}
	} else {
		addrs = [][2]string{{"unix", *sockPath}}
	}
	if defPacket && runtime.GOOS == "darwin" {
		// OS X does not support Unix packet. Ignore it if it was implicit.
		for i := 0; i < len(addrs); i++ {
			if addrs[i][0] == "unixpacket" {
				addrs = append(addrs[0:i], addrs[i+1:]...)
				i--
			}
		}
	}
	for _, a := range addrs {
		l, err := listenUnix(a[0], a[1])
		if err != nil {
			log.Fatal(err)
		}
		defer l.Close()
		go listen(&sessionStore, &dataStore, l, a[0] == "unixpacket")
	}

	self, err := newSelfSession(&dataStore, &sessionStore)
	if err != nil {
		log.Fatal(err)
	}
	log.SetOutput(self)

	if pidF != nil {
		fmt.Fprintf(pidF, "%d\n", os.Getpid())
		pidF.Close()
	}

	err = systemdMessage("READY=1")
	if err != nil {
		log.Printf("Error sending READY to systemd: %v", err)
	}

	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	<-sigCh

	graceful := make(chan bool)
	go func() { Work.Wait(); graceful <- true }()
	select {
	case <-graceful:
	case <-time.After(5 * time.Second):
		log.Print("Timed out waiting for sessions to finish.")
	}
	for {
		select {
		case quitDoneCh <- struct{}{}:
			time.Sleep(100 * time.Millisecond)
		default:
			return
		}
	}

}
