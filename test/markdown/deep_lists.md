# Deep Lists

A document that exercises deeply nested and long lists, the kind of
content `MarkdownReaderActivity` will see when rendering issue/PR
checklists from the GitHub Companion mode in later phases.

- Top-level item one with a fairly long description that should wrap
  across two or more lines once the renderer applies the bullet indent
  and word-wrap.
- Top-level item two
  - Nested item A
  - Nested item B with **bold** highlighting and `inline code`
- Top-level item three

## Numbered task list

1. Investigate the failure mode reported in the latest CI run.
2. Reproduce locally with the same firmware build.
3. Patch the suspected regression.
4. Add regression coverage.
5. Verify the fix resolves the original symptom.
6. Update documentation if behaviour visible to users changed.
7. Open a pull request and request review from the relevant team.
8. Merge once approved and CI is green again.

## Mixed content list

- A paragraph-style item that contains *italic*, **bold**, and a
  [link](https://example.com/) so the wrapping logic must cope with a
  variety of styled runs in a single physical row.
- A second item that is intentionally short.
