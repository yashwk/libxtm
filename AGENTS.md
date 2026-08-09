---
name: Workspace Permissions
description: Grants necessary permissions for the agent in this workspace.
---

```text
allow:
  read_file(*)
  read_url(*)
  command(ls)
  command(cat)
  command(grep)
  command(find)
  command(head)
  command(tail)
  command(cmake)
  command(git (status|log|diff|show))
```
