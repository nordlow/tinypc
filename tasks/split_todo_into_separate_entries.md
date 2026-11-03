# Split root `TODO` file into separate entries under `tasks/`

> **Status:** In progress. `tasks/` already exists; the root `TODO.md`
> still contains untriaged items.

Migrate and split the items currently listed in the monolithic root
`TODO.md` into individual, descriptive markdown entries under the
`tasks/` directory, then remove or deprecate the root file once every
item has a home.

## Goal

- One topic per task file, with a stable descriptive name.
- No information loss during migration.
- `TODO.md` becomes either a short index or is deleted.

## Implementation steps

1. **Inventory.** List every heading, bullet, and proposal in
   `TODO.md`; assign a temporary ID.
2. **Classify each item** as:
   - already implemented (link the test/commit, then drop it),
   - duplicate of an existing `tasks/*.md` (merge/link),
   - new task (create a file),
   - not actionable (drop with a one-line note).
3. **Name files** with the existing style: lowercase, underscores,
   topic-first, `.md`.
4. **Create task files** using the project template:
   - status line,
   - goal/non-goals,
   - fine-grained implementation steps,
   - edge cases,
   - test plan,
   - acceptance criteria.
5. **Cross-link** related tasks in a `Related` section.
6. **Update `TODO.md`** after each batch: replace migrated items with
   links until the file is empty, then delete it.
7. **Verify** no task file references a deleted heading by number.

## Mapping table (fill during migration)

| TODO item | destination | status |
| --- | --- | --- |
| example proposal | `tasks/example.md` | migrated |

## Edge cases

- Items that span two features: split into two files and link them.
- Items that are actually bugs: move to the issue tracker instead.
- Historical notes: preserve in the task body, not as a floating
  `TODO` comment.
- Performance measurements: move to `bench/` or a task appendix.

## Acceptance criteria

- [ ] Every `TODO.md` item is migrated, merged, or explicitly dropped.
- [ ] `tasks/` has no duplicate topics.
- [ ] Each new task follows the detailed template.
- [ ] `TODO.md` is deleted or reduced to an index.
