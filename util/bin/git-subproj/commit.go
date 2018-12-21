// Copyright 2018 Schibsted

package main

import (
	"bytes"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"path/filepath"
)

var (
	commitMessage = flag.String("message", "", "Commit message to use.")
	tag           = flag.String("tag", "", "Create annotated tags. The base name of each sub project will be prepended with a slash.")
	tagMessage    = flag.String("tag-message", "", "Tag message to use.")
)

func commitSubprojs(confs []*subprojConfig) {
	var commitMsg []byte

	if *commitMessage != "" {
		commitMsg = []byte(*commitMessage)
	} else {
		var message bytes.Buffer

		fmt.Fprint(&message, "Enter your commit message.\n\n")

		for _, conf := range confs {
			// Assume this is cached by now.
			_, parentTree := fetchRemote(conf.Repo, conf.Branch)

			diff := execCommand(nil, "git", "diff", "--stat", parentTree, conf.tree)
			fmt.Fprintf(&message, "Changes for %s:\n%s", conf.Subproj, diff)

			shortParent := execStringCommand(nil, "git", "rev-parse", "--short", parentTree)
			shortTree := execStringCommand(nil, "git", "rev-parse", "--short", conf.tree)
			fmt.Fprintf(&message, "Inspect with: git diff %s %s\n\n", shortParent, shortTree)
		}

		commitMsg = editMessage("SUBPROJ_COMMIT_MSG", message.Bytes())
	}

	for _, conf := range confs {
		// Assume this is cached by now.
		parentCommit, _ := fetchRemote(conf.Repo, conf.Branch)

		conf.commit = execStringCommand(commitMsg, "git", "commit-tree", "-p", parentCommit, conf.tree)
	}
}

// Check if any of the tags already exist.
func checkTags(confs []*subprojConfig) {
	fatal := false
	for _, conf := range confs {
		tagName := filepath.Base(conf.Subproj) + "/" + *tag
		err := exec.Command("git", "show-ref", "--verify", "--quiet", "--", "refs/tags/"+tagName).Run()
		if err == nil {
			fmt.Fprintf(os.Stderr, "The tag %s already exists.\n", tagName)
			fatal = true
		} else {
			execerr, ok := err.(*exec.ExitError)
			if !ok || !execerr.Exited() {
				log.Fatal(err)
			}
		}
	}
	if fatal {
		os.Exit(1)
	}
}

func commitTags(confs []*subprojConfig) {
	var tagMsg []byte

	tagger := execStringCommand(nil, "git", "var", "GIT_COMMITTER_IDENT")

	if *tagMessage != "" {
		tagMsg = []byte(*tagMessage)
	} else {
		var message bytes.Buffer

		fmt.Fprint(&message, "Enter tag description.\n\nTags to create:\n")
		for _, conf := range confs {
			fmt.Fprintf(&message, "  %s/%s\n", filepath.Base(conf.Subproj), *tag)
		}

		tagMsg = editMessage("SUBPROJ_TAG_MSG", message.Bytes())
	}

	for _, conf := range confs {
		tagName := filepath.Base(conf.Subproj) + "/" + *tag

		var tagContents bytes.Buffer
		fmt.Fprintf(&tagContents, "object %s\n", conf.commit)
		fmt.Fprintf(&tagContents, "type commit\n")
		fmt.Fprintf(&tagContents, "tag %s\n", tagName)
		fmt.Fprintf(&tagContents, "tagger %s\n", tagger)
		fmt.Fprintf(&tagContents, "\n%s", tagMsg)

		tagobj := execStringCommand(tagContents.Bytes(), "git", "mktag")
		execCommand(nil, "git", "update-ref", "refs/tags/"+tagName, tagobj)
	}
}

func editMessage(fileName string, message []byte) []byte {
	editMsg := execCommand(message, "git", "stripspace", "-c")
	editMsg = append([]byte("\n"), editMsg...)

	editMsgPath := gitDir + "/" + fileName
	err := ioutil.WriteFile(editMsgPath, editMsg, 0666)
	if err != nil {
		log.Fatal(err)
	}

	editor := execStringCommand(nil, "git", "var", "GIT_EDITOR")

	cmd := exec.Command(editor, editMsgPath)
	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	err = cmd.Run()
	if err != nil {
		log.Fatal(err)
	}

	editMsg, err = ioutil.ReadFile(editMsgPath)
	if err != nil {
		log.Fatal(err)
	}
	os.Remove(editMsgPath)

	editMsg = execCommand(editMsg, "git", "stripspace", "-s")
	if len(editMsg) == 0 {
		fmt.Fprintf(os.Stderr, "Empty message, aborting.")
		os.Exit(1)
	}

	return editMsg
}
