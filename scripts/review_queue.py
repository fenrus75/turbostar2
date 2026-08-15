#!/usr/bin/env python3
import json
import os
import sys
import argparse

REVIEW_FILE = "review.json"

def load_db():
    if not os.path.exists(REVIEW_FILE):
        print(f"Error: {REVIEW_FILE} not found.")
        sys.exit(1)
    with open(REVIEW_FILE, "r", encoding="utf-8") as f:
        return json.load(f)

def save_db(data):
    # Write atomically via temp file
    tmp_path = REVIEW_FILE + ".tmp"
    with open(tmp_path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=4, ensure_ascii=False)
        f.write("\n")
    os.replace(tmp_path, REVIEW_FILE)

def cmd_summary(args):
    data = load_db()
    items = data.get("items", [])
    dirs = {}
    for i in items:
        d = os.path.dirname(i.get("filename", ""))
        if d not in dirs:
            dirs[d] = []
        dirs[d].append(i)
    
    print(f"Total Review Items: {len(items)}")
    print(f"{'OPEN':>6} / {'TOTAL':>6}  --> DIRECTORY")
    print("-" * 60)
    for d, item_list in sorted(dirs.items(), key=lambda x: -len(x[1])):
        open_c = sum(1 for i in item_list if i.get("state") in ("new", "confirmed"))
        if open_c > 0 or args.all:
            print(f"{open_c:6d} / {len(item_list):6d}  --> {d}")

def cmd_list(args):
    data = load_db()
    items = data.get("items", [])
    matched = []
    for i in items:
        if i.get("state") not in ("new", "confirmed") and not args.all:
            continue
        if args.dir and not i.get("filename", "").startswith(args.dir):
            continue
        matched.append(i)

    print(f"Found {len(matched)} matching open items:")
    for i in matched:
        print(f"  [ID {i.get('id'):3d}] {i.get('filename')}:{i.get('line_number')} - {i.get('summary')} ({i.get('severity')})")

def cmd_show(args):
    data = load_db()
    items = data.get("items", [])
    for i in items:
        if i.get("id") == args.id:
            print(f"Item ID: {i.get('id')}")
            print(f"State: {i.get('state')}")
            print(f"File: {i.get('filename')}:{i.get('line_number')}")
            print(f"Severity: {i.get('severity')}")
            print(f"Summary: {i.get('summary')}")
            print(f"Line Content: {i.get('line_content')}")
            print("-" * 60)
            print("Description:")
            print(i.get('description'))
            print("-" * 60)
            print("Proposed Fix:")
            print(i.get('proposed_fix'))
            return
    print(f"Item ID {args.id} not found.")

def cmd_resolve(args):
    data = load_db()
    items = data.get("items", [])
    for i in items:
        if i.get("id") == args.id:
            i["state"] = "resolved"
            if args.commit:
                i["resolved_in_commit"] = args.commit
            save_db(data)
            print(f"Item ID {args.id} marked as 'resolved' (commit: {args.commit or 'N/A'}).")
            return
    print(f"Item ID {args.id} not found.")

def cmd_invalid(args):
    data = load_db()
    items = data.get("items", [])
    for i in items:
        if i.get("id") == args.id:
            i["state"] = "invalid"
            if args.reason:
                i["description"] = f"{i.get('description', '')}\n\n[INVALID REASON]: {args.reason}"
            save_db(data)
            print(f"Item ID {args.id} marked as 'invalid' (reason: {args.reason or 'N/A'}).")
            return
    print(f"Item ID {args.id} not found.")

def main():
    parser = argparse.ArgumentParser(description="Turbostar Review Queue CLI")
    subparsers = parser.add_subparsers(dest="command")

    p_sum = subparsers.add_parser("summary", help="Show directory summary")
    p_sum.add_argument("--all", action="store_true", help="Include directories with 0 open items")

    p_list = subparsers.add_parser("list", help="List review items")
    p_list.add_argument("--dir", help="Filter by directory prefix")
    p_list.add_argument("--all", action="store_true", help="Include non-open items")

    p_show = subparsers.add_parser("show", help="Show details of a specific item")
    p_show.add_argument("--id", type=int, required=True, help="Item ID")

    p_res = subparsers.add_parser("resolve", help="Mark an item as resolved")
    p_res.add_argument("--id", type=int, required=True, help="Item ID")
    p_res.add_argument("--commit", help="Git commit hash")

    p_inv = subparsers.add_parser("invalid", help="Mark an item as invalid")
    p_inv.add_argument("--id", type=int, required=True, help="Item ID")
    p_inv.add_argument("--reason", help="Explanation why this review item is invalid")

    args = parser.parse_args()
    if args.command == "summary":
        cmd_summary(args)
    elif args.command == "list":
        cmd_list(args)
    elif args.command == "show":
        cmd_show(args)
    elif args.command == "resolve":
        cmd_resolve(args)
    elif args.command == "invalid":
        cmd_invalid(args)
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
