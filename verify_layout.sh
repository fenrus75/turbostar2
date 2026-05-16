#!/bin/bash
set -e
tmux start-server
PANE_ID=$(tmux new-window -d -P -F '#{pane_id}' './build/turbostar')
sleep 1
tmux send-keys -t "$PANE_ID" M-f o
sleep 1
tmux capture-pane -p -t "$PANE_ID"
tmux kill-window -t "$PANE_ID"
