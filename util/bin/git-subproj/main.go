// Copyright 2018 Schibsted

package main

import (
	"bufio"
	"bytes"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	yaml "gopkg.in/yaml.v2"
)

type subprojConfig struct {
	Subproj      string                // Path in parent tree to subproject
	Repo, Branch string                // Sub project repo and default branch
	Paths        map[string]pathConfig // Path inside subproject with config.

	tree   string // Internally used to store the newly generated tree.
	commit string // Internally used to store the newly generated commit.
}

type pathConfig struct {
	Repo, Ref string // Fetch from here if not available in parent project.
	Path      string // Path in parent project or fetched one.
	Subproj   string // Load tree from another subproj. Sets Tree and thus overrides everything else.

	name string // name inside directory, filled in while parsing.
	tree string // A specific tree object, internal use only for now.
}

var (
	configfile = ""
	branchname = ""
	quiet      = false
	traceGit   = false
)

var (
	gitDir string
)

func main() {
	flag.StringVar(&configfile, "config", configfile, "Config file to use, the default is .subproj.yml in the root worktree directory.")
	flag.StringVar(&branchname, "branch", branchname, "Override the branch to use in the sub projects. Note: This applies to all chosen sub projects.")
	flag.BoolVar(&quiet, "quiet", quiet, "Don't print diagnostics")
	flag.BoolVar(&traceGit, "git-trace", traceGit, "Show all git commands and i/o (quite verbose)")
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage of %s:\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "\t%s [<flags>] [<subproject>...]\n", os.Args[0])
		flag.PrintDefaults()
	}
	flag.Parse()

	var confdata []byte
	var err error
	if configfile != "" {
		confdata, err = ioutil.ReadFile(configfile)
		if err != nil {
			log.Fatal("Read ", configfile, ": ", err)
		}
	}

	topdir := execStringCommand(nil, "git", "rev-parse", "--show-toplevel")
	if topdir == "" {
		log.Fatal("Failed to find worktree top level")
	}
	err = os.Chdir(topdir)
	if err != nil {
		log.Fatal("Failed to cd to worktree top level", err)
	}
	gitDir = execStringCommand(nil, "git", "rev-parse", "--git-dir")
	if gitDir == "" {
		log.Fatal("Failed to find .git directory")
	}

	if configfile == "" {
		confdata, err = ioutil.ReadFile(".subproj.yml")
		if err != nil {
			log.Fatalf("Read %s/.subproj.yml: %v", topdir, err)
		}
	}

	var subprojs []*subprojConfig
	err = yaml.Unmarshal(confdata, &subprojs)
	if err != nil {
		log.Fatal(err)
	}

	if flag.NArg() > 0 {
		subprojs = filterSubprojs(subprojs)
	}

	tocommit := make([]*subprojConfig, 0, len(subprojs))

	for _, conf := range subprojs {
		if branchname != "" {
			conf.Branch = branchname
		}
		tree := buildSubproject(conf)
		if tree != "" {
			conf.tree = tree
			tocommit = append(tocommit, conf)
		}
	}

	if len(tocommit) == 0 {
		fmt.Printf("Nothing to commit.\n")
		return
	}

	if *tag != "" {
		// Check if tags exist before committing.
		checkTags(tocommit)
	}

	commitSubprojs(tocommit)

	if *tag != "" {
		commitTags(tocommit)
	}

	if !quiet {
		for _, conf := range tocommit {
			fmt.Printf("subproject: %s commit: %s", conf.Subproj, conf.commit)
			if *tag != "" {
				fmt.Printf(" tag: %s/%s", filepath.Base(conf.Subproj), *tag)
			}
			fmt.Printf("\n")
		}
	}
	for _, conf := range tocommit {
		shortcommit := execStringCommand(nil, "git", "rev-parse", "--short", conf.commit)
		fmt.Printf("git push %s %s:%s", conf.Repo, shortcommit, conf.Branch)
		if *tag != "" {
			fmt.Printf(" %s/%s", filepath.Base(conf.Subproj), *tag)
		}
		fmt.Printf("\n")
	}
}

func filterSubprojs(subprojs []*subprojConfig) []*subprojConfig {
	filterto := flag.Args()
	for i := 0; i < len(subprojs); {
		conf := subprojs[i]
		found := false
		for j, candidate := range filterto {
			// Match either on full path or on basename.
			// Also remove any trailing slash that was probably added by tab completion.
			candidate := strings.TrimSuffix(candidate, "/")
			switch {
			case candidate == conf.Subproj:
				found = true
			case candidate == filepath.Base(conf.Subproj):
				found = true
			}
			if found {
				filterto = append(filterto[:j], filterto[j+1:]...)
				break
			}
		}
		if found {
			i++
		} else {
			subprojs = append(subprojs[:i], subprojs[i+1:]...)
		}
	}
	if len(filterto) > 0 {
		fmt.Fprintf(os.Stderr, "Subproject not found in config: %s\n", strings.Join(filterto, ", "))
		os.Exit(1)
	}
	return subprojs
}

type confTree struct {
	subConfs map[string]*confTree
	confs    []pathConfig
}

var subprojTrees = make(map[string]string)

func buildSubproject(subproj *subprojConfig) (tree string) {
	var deepconf confTree

	_, parentTree := fetchRemote(subproj.Repo, subproj.Branch)

	for subpath, buildconf := range subproj.Paths {
		insertConf(&deepconf, subpath, buildconf)
	}

	tree = buildConfTree(subproj.Subproj, "", &deepconf)

	subprojTrees[subproj.Subproj] = tree

	if tree == parentTree {
		if !quiet {
			fmt.Fprintf(os.Stderr, "No changes for %s, skipping\n", subproj.Subproj)
		}
		return ""
	}

	return tree
}

// Recurse on / in path and insert into the target config tree.
func insertConf(target *confTree, path string, conf pathConfig) {
	split := strings.Split(path, "/")
	for len(split) > 1 {
		if target.subConfs == nil {
			target.subConfs = make(map[string]*confTree)
		}
		next := target.subConfs[split[0]]
		if next == nil {
			next = new(confTree)
			target.subConfs[split[0]] = next
		}
		target = next
		split = split[1:]
	}
	conf.name = split[0]
	target.confs = append(target.confs, conf)
}

func buildConfTree(subproj, subpath string, conftree *confTree) string {
	// Flatten subpaths by building them to trees.
	for path, conf := range conftree.subConfs {
		tree := buildConfTree(subproj, filepath.Join(subpath, path), conf)
		newconf := pathConfig{tree: tree}
		insertConf(conftree, path, newconf)
	}

	return buildTree("HEAD", subproj, subpath, conftree.confs)
}

// Modify the subpath inside subproj with the contents from config
func buildTree(ref, subproj, subpath string, config []pathConfig) (tree string) {
	fullpath := filepath.Join(subproj, subpath) + "/"
	treeData := execCommand(nil, "git", "ls-tree", ref, fullpath)

	var mktree bytes.Buffer

	// Add the configured contents.
	namefilter := make(map[string]bool)
	for _, conf := range config {
		namefilter[conf.name] = true

		if conf.Subproj != "" {
			conf.tree = subprojTrees[conf.Subproj]
			if conf.tree == "" {
				log.Fatal("Can only load trees from subproject earlier in file.")
			}
		}

		if conf.tree == "" {
			cRef := ref
			if conf.Repo != "" {
				fetchref := conf.Ref
				if fetchref == "" {
					fetchref = ref
				}
				_, cRef = fetchRemote(conf.Repo, fetchref)
			}
			if conf.Path == "" {
				conf.tree = execStringCommand(nil, "git", "rev-parse", cRef+"^{tree}")
			} else {
				cData := execCommand(nil, "git", "ls-tree", cRef, conf.Path)
				fields := strings.Fields(string(cData))
				if len(fields) != 4 {
					log.Fatal("Error, bad line: ", string(cData))
				}
				conf.tree = fields[2]
			}
		}

		fmt.Fprintf(&mktree, "040000 tree %s\t%s\n", conf.tree, conf.name)
	}

	// Add the existing contents, minus the configs above.
	scanner := bufio.NewScanner(bytes.NewReader(treeData))
	for scanner.Scan() {
		fields := strings.Fields(scanner.Text())
		if len(fields) != 4 {
			if len(fields) > 0 {
				log.Print("Warning, bad line: ", scanner.Text())
			}
			continue
		}
		mode := fields[0]
		etype := fields[1]
		obj := fields[2]
		name := filepath.Base(fields[3])

		if namefilter[name] {
			continue
		}

		fmt.Fprintf(&mktree, "%s %s %s\t%s\n", mode, etype, obj, name)
	}

	return execStringCommand(mktree.Bytes(), "git", "mktree")
}

var remotesCache = make(map[[2]string][2]string)

// Fetch a commit and tree based on a repository and branch name.
func fetchRemote(repo, ref string) (commit, tree string) {
	cache := remotesCache[[2]string{repo, ref}]
	if cache[0] != "" {
		return cache[0], cache[1]
	}

	_ = execCommand(nil, "git", "fetch", repo, ref)
	commit = execStringCommand(nil, "git", "rev-parse", "FETCH_HEAD^{commit}")
	tree = execStringCommand(nil, "git", "rev-parse", commit+"^{tree}")

	remotesCache[[2]string{repo, ref}] = [2]string{commit, tree}
	return
}

func execStringCommand(stdin []byte, argv ...string) string {
	return string(bytes.TrimSpace(execCommand(stdin, argv...)))
}

func execCommand(stdin []byte, argv ...string) (stdout []byte) {
	if traceGit {
		fmt.Fprintf(os.Stderr, "+ %v\n", argv)
	}
	cmd := exec.Command(argv[0], argv[1:]...)
	if stdin != nil {
		if traceGit {
			scanner := bufio.NewScanner(bytes.NewReader(stdin))
			for scanner.Scan() {
				fmt.Fprintf(os.Stderr, "< %s\n", scanner.Text())
			}
		}
		cmd.Stdin = bytes.NewReader(stdin)
	}
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	out, err := cmd.Output()
	if err != nil {
		log.Printf("%v failed: %v", argv, err)
		os.Stderr.Write(out)
		stderr.WriteTo(os.Stderr)
		os.Exit(1)
	}
	if traceGit {
		scanner := bufio.NewScanner(bytes.NewReader(out))
		for scanner.Scan() {
			fmt.Fprintf(os.Stderr, "> %s\n", scanner.Text())
		}
		scanner = bufio.NewScanner(&stderr)
		for scanner.Scan() {
			fmt.Fprintf(os.Stderr, "> %s\n", scanner.Text())
		}
	}
	return out
}
