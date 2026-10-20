# Release checklist

Before tagging a **new version**:

1. Run the tests with `make test`.
2. Update `CHANGELOG.md` and *src/version.h*.
3. Check the build with [GitHub Actions](https://github.com).

```python
def bump(version: str) -> str:
    major, minor, patch = map(int, version.split("."))
    return f"{major}.{minor}.{patch + 1}"
```

```sh
git tag v1.0.1 && git push origin v1.0.1
```

> The release workflow builds and publishes the release.
