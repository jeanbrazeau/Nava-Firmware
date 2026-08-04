# .github/workflows/

## Files

| File | What | When to read |
| ---- | ---- | ------------ |
| `README.md` | Why the release is built on a runner, why the tag is checked against the splash, why `main.yml` is kept | Changing what a release publishes; deciding whether `main.yml` can go |
| `release.yml` | Builds a version tag with PlatformIO, decodes the `.syx` to prove it is a valid image, publishes it as `nava-<tag>.syx` | Changing the build, the verification, the release notes or the install line they print |
| `main.yml` | Inherited from the upstream Nava2021 project: Windows, arduino-cli, `workflow_dispatch` only | Do not extend it - it builds nothing on its own and refers to a tree that is not here |

## Trigger

`release.yml` runs on version tags only (`[0-9]*.[0-9]*`), pushed by
`scripts/release.py`. Nothing here runs on a push to a branch or on a pull
request.
