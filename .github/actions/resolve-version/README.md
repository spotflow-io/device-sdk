# GitHub Action: Resolve SemVer2 version based on branch, tag and PR context

This GitHub actions resolves version that can be used by subsequent jobs to e.g. build and publish artifacts. The versioning strategy follows [Semantic Versioning 2.0](https://semver.org/) and the [GitVersion](https://gitversion.net/) tool is used to generate version strings (as .NET global tool) together with a bit of custom logic for generating prerelease versions.

## Usage

```yml
init:
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4
      with:
          fetch-depth: 0
    - uses: ./.github/actions/resolve-version
      id: resolve-version
      with:
        tag-prefix: observability_v # Optional
    - run: echo ${{ steps.resolve-version.outputs.version }}
```

## Versioning strategy

The `alpha` and `beta` prerelease identifiers without build identifiers are used:

### Alpha pre-releases for Pull Requests

`<major>.<minor>.<patch>-alpha<pr>.<#-of-commits>.<github-run-number>.<github-run-attempt>`

where `<pr>` is the Pull Request number (padded to 4 characters) and `<#-of-commits>` is the number of commits from the last tag. For details about `<github-run-number>` and `<github-run-attempt>`, see [GitHub Contexts](https://docs.github.com/en/actions/learn-github-actions/contexts#github-context).

Created on pushes into PR branches.

Examples:

* `1.0.0-alpha9.18.4.1`
* `0.0.1-alpha9.18.4.1`
* `0.14.4-alpha9.18.4.1`

### Alpha pre-releases for feature/hotfix branches

`<major>.<minor>.<patch>-alpha.<#-of-commits>.<github-run-number>.<github-run-attempt>`

where `<#-of-commits>` is the number of commits from the last tag. For details about `<github-run-number>` and `<github-run-attempt>`, see [GitHub Contexts](https://docs.github.com/en/actions/learn-github-actions/contexts#github-context).

Created on pushes into feature/hotfix branches.

Examples:

* `1.0.0-alpha.18.4.1`
* `0.0.1-alpha.18.4.1`
* `0.14.4-alpha.18.4.1`

### Beta pre-releases

 `<major>.<minor>.<patch>-beta.<#-of-commits>`

 where `<major>.<minor>.<patch>` is determined from the last tag by increasing `<patch>` by 1 and `<#-of-commits>` is number of commits since the last tag.

Created on each push to `main` branch.

Examples:
* `1.0.0-beta.9`
* `0.0.1-beta.9`
* `0.14.4-beta.9`

### Releases

 `<major>.<minor>.<patch>`

Created only when git tag `v<major>.<minor>.<patch>` is created.

Examples:

* `1.0.0`
* `0.0.1`
* `0.14.4`

## Input parameters

* `git-version-tool-version` (string, optional): Version of the GitVersion.Tool

## Output parameters

* `version` (string): Resolved SemVer2 version

## Local run

Main logic is contained in [Resolve-Version.ps1](./Resolve-Version.ps1) script that can be run locally.

## FAQ

* *Wouldn't be enough to just use built-in `gitversion` functionality instead of combining with custom logic?* It would be great, but we have not found a way how to inject information about GitHub run into it and other approaches, such as using number of commits are not robust enough on feature branches (e.g. due to force pushes).

* *Why are `<github-run-*>` identifiers not zero-padded to be properly ordered in various UIs?* Because [Semantic Versioning 2.0](https://semver.org/) requires that dot-separated prerelease identifiers compared numerically.

## Releases & Versioning of this GitHub Action

* New release `vX.Y.Z` is created for each relevant change or batch of changes manually.
* Tag `vX` -> `vX.YZ` is (re-)created automatically for each new release to allow consuming workflows to depend on the major version only.
