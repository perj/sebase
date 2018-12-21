git-subproj(1) -- create commits for sub-projects
=================================================

## SYNOPSIS

`git-subproj` [ `-config` file ] [ `-branch` name ] [ `-message` msg ]
  [ `-tag` name ] [ `-tag-message` msg ] [ `-quiet` ] [ `-git-trace` ]
  [subproject...]

## DESCRIPTION

This tool is used to create commits for sub-projects, which can then be pushed
to the repository for each project. Optionally it can also create annotated
tags to go along with the commit.

By default all sub-projects in the configuration file will be processed, but
you can limit it to a smaller set by specifying `subproject` as arguments,
multiple ones are accepted. You can either give the full path to the sub-project
or just the base directory name.

Only those sub-projects that have any changes will have commits and tags created.

The output will consist of some diagnostic messages, unless `-quiet` is used,
and finally instructions for how to push the newly created objects to each
repoistory. The push will not actually be done.

## OPTIONS

* `-config` file:
  Specify the configuration file to use. By default `.subproj.yml` in the
  root of the worktree is used.

* `-branch` name:
  Override the branch set in the config file and compare vs. this branch
  instead. Note that this option is applied to all sub-projects so it's best
  used together with a limited set given as arguments.

* `-message` msg:
  Use this as the commit message. By default the git editor is opened similar
  to **git commit**. You can only specify a single message which is used for
  all the sub-projects.

* `-tag` name:
  Create an annotated tag pointing to the newly created commit. You will
  be asked for a tag description, unless you use `-tag-message`.

* `-tag-message` msg:
  Use this as the tag description. By default the git editor is opened similar
  to when creating an annotated tag. Only applies if `-tag` was also used.

* `-quiet`:
  Suppress additional messages, such as ignoring a sub-project with no changes.

* `-git-trace`:
  Trace all git commands run and their input/output. This can be very verbose,
  use only for troubleshooting.
  Commands run and errors are prefixed with a `+`. Input sent to the command
  is prefixed with `<`, while output from the command is prefixed with `>`.

## CONFIGURATION

The configuration file is a yaml file. The top level should be a list of
sub-projects, which are dictionaries with these elements:

* `subproj`:
  The path in the parent project to the sub-project.

* `repo`:
  The git repository for the sub-project. This is used to compare changes and
  to find the parent commit for the new one.

* `branch`:
  The branch to compare against / create commits for.

* `paths`:
  An optional dictionary of paths inside the sub-projects to create. See below.

### Paths config

In each sub-project, you can create directories containing paths from the parent
main project tree. For example, if you have two sub-projects and one depends on
the other, you can vendor the directory of the second project inside that of
the first.

Keys inside the paths dictionary are the paths inside the sub-project. These
paths will be created in the new commit, but don't necessarily exist in the
main project tree (if they do exist then the contents will be ignored when
creating the commit).

These elements can be used for each path:

* `repo`: Optional git repostiory to fetch the path from, by default the
  current HEAD of the main worktree is used.

* `ref`: Reference (e.g. branch or tag name) to fetch. Only applies together
  with `repo`.

* `path`: The path inside the main project tree or fetched repo to copy
  to this location. Use an empty path for the top level directory.

* `subproj`: Optionally, instead of using the above elements, you can specify
  another sub-project inside the config file to copy to this path. If this is
  used all other keys are ignored.

### Example config

Here `sub/a` and `sub/b` are sub-projects. `sub/b` is configured to contain
a copy of `sub/a` at the path `vendor/a`, and also to contain a copy of
gemoji at `vendor/gemoji`.

```yaml
- subproj: sub/a
  repo: git@git.example.com:myorg/a
  branch: master

- subproj: sub/b
  repo: git@git.example.com:myorg/b
  branch: master
  paths:
    vendor/a:
      path: sub/a
    vendor/gemoji:
      repo: git@github.com:github/gemoji.git
      ref: master
      path:
```

## COPYRIGHT

Copyright 2018 Schibsted
