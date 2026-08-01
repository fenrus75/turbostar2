# Code Review Subagent Workflow

## Guidelines
- **File Slicing**: Do not review more than 10 files or 1,500 lines of code in a single subagent call. Group larger file lists logically and invoke `perform_code_review` separately for each group.
- **Checklists & Instructions**: Provide overall context in `instructions` and supply a checklist of specific review items in the `todos` vector.
- **Post-Review**: Call `list_code_review_items` to get a concise summary table of active findings, and `get_code_review_item` to retrieve details.
- **Resolution**: Use `resolve_code_review_item` when issues are addressed or ruled out.
