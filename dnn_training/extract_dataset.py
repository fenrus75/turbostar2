#!/usr/bin/env python3
import os
import glob
import json
import re

def get_last_50_words(text):
    words = text.split()
    if len(words) <= 50:
        return text
    return " ".join(words[-50:])

def get_turns_and_boundaries(history_dir, episode_file, visited=None):
    if visited is None:
        visited = set()
    abs_path = os.path.abspath(episode_file)
    if abs_path in visited:
        return []
    visited.add(abs_path)

    try:
        with open(episode_file, 'r') as f:
            data = json.load(f)
    except Exception as e:
        print(f"Error loading {episode_file}: {e}")
        return []
        
    convo = data.get('conversation', [])
    turns = []
    current_turn = None
    
    for msg in convo:
        role = msg.get('role')
        content = msg.get('content', '')
        
        # Check if it's a pointer message
        if role == 'system' and '[SYSTEM MEMORY: Episode Archived]' in content:
            # First, flush any current turn
            if current_turn:
                turns.append(current_turn)
                current_turn = None
                
            episode_id = msg.get('episode_id')
            if not episode_id:
                m = re.search(r'Raw history archive:\s*(\w+)', content)
                if m:
                    episode_id = m.group(1)
            
            if episode_id:
                ref_file = os.path.join(history_dir, f"{episode_id}.json")
                if os.path.exists(ref_file):
                    ref_turns = get_turns_and_boundaries(history_dir, ref_file, visited)
                    if ref_turns:
                        # The last turn of the referenced episode is a boundary!
                        ref_turns[-1]['is_boundary'] = True
                        turns.extend(ref_turns)
            continue
            
        if role == 'user':
            if current_turn:
                turns.append(current_turn)
            current_turn = {
                'prompt': content,
                'response': '',
                'timestamp': msg.get('timestamp', 0),
                'duration_ms': msg.get('duration_ms', 0),
                'is_boundary': False,
                'git_commit': False,
                'compile': False,
                'test': False
            }
        elif role == 'assistant' and current_turn:
            if content:
                if current_turn['response']:
                    current_turn['response'] += '\n' + content
                else:
                    current_turn['response'] = content
            if msg.get('duration_ms', 0) > 0:
                current_turn['duration_ms'] += msg.get('duration_ms')
            if msg.get('timestamp', 0) > 0:
                current_turn['timestamp'] = msg.get('timestamp')
            
            # Check tool calls
            tool_calls = msg.get('tool_calls', [])
            if tool_calls:
                for tc in tool_calls:
                    name = tc.get('function', {}).get('name', '')
                    if name == 'git_commit':
                        current_turn['git_commit'] = True
                    elif name in ('fs_compile_project', 'fs_compile_file'):
                        current_turn['compile'] = True
                    elif name == 'fs_run_tests':
                        current_turn['test'] = True
        elif role == 'tool' and current_turn:
            name = msg.get('name', '')
            if name in ('fs_compile_project', 'fs_compile_file'):
                if "Error:" not in content and "FAILED" not in content:
                    current_turn['compile'] = True
            elif name == 'fs_run_tests':
                if "Error:" not in content and "FAILED" not in content:
                    current_turn['test'] = True
            
    if current_turn:
        turns.append(current_turn)
        
    return turns

def extract_samples(history_dir):
    active_file = os.path.join(history_dir, "active_state.json")
    if not os.path.exists(active_file):
        return []
        
    turns = get_turns_and_boundaries(history_dir, active_file)
    if len(turns) < 2:
        return []
        
    samples = []
    active_tokens = 0
    
    for i in range(1, len(turns)):
        prev_turn = turns[i-1]
        curr_turn = turns[i]
        
        # 1. Input T-1 context
        t_prev_text = prev_turn['prompt'] + " [Agent Conclusion: ] " + get_last_50_words(prev_turn['response'])
        
        # 2. Input T prompt
        t_curr_text = curr_turn['prompt']
        
        # 3. Metadata features M (16-dim)
        # 3.1 idle_to_prompt Time Gap
        gap_sec = 0.0
        if curr_turn['timestamp'] > 0 and prev_turn['timestamp'] > 0:
            gap_sec = curr_turn['timestamp'] - (prev_turn['timestamp'] + prev_turn['duration_ms'] / 1000.0)
            
        idle_onehot = [0.0, 0.0, 0.0]
        if gap_sec < 60.0:
            idle_onehot[0] = 1.0
        elif gap_sec < 300.0:
            idle_onehot[1] = 1.0
        else:
            idle_onehot[2] = 1.0
            
        # 3.2 agent_thinking_time Duration
        think_sec = prev_turn['duration_ms'] / 1000.0
        think_onehot = [0.0, 0.0, 0.0]
        if think_sec < 10.0:
            think_onehot[0] = 1.0
        elif think_sec < 120.0:
            think_onehot[1] = 1.0
        else:
            think_onehot[2] = 1.0
            
        # 3.3 token_pressure Level
        # Estimate tokens of previous turn to add to active pressure
        prev_tokens = len(prev_turn['prompt'] + prev_turn['response']) / 4.0
        active_tokens += prev_tokens
        
        pressure_ratio = active_tokens / 8192.0
        pressure_onehot = [0.0, 0.0, 0.0, 0.0]
        if pressure_ratio < 0.60:
            pressure_onehot[0] = 1.0
        elif pressure_ratio < 0.80:
            pressure_onehot[1] = 1.0
        elif pressure_ratio < 0.95:
            pressure_onehot[2] = 1.0
        else:
            pressure_onehot[3] = 1.0
            
        # 3.4 deterministic_flags
        flags = [
            1.0 if prev_turn['git_commit'] else 0.0,
            1.0 if prev_turn['compile'] else 0.0,
            1.0 if prev_turn['test'] else 0.0
        ]
        
        # 3.5 padding
        padding = [0.0, 0.0, 0.0]
        
        m_vector = idle_onehot + think_onehot + pressure_onehot + flags + padding
        assert len(m_vector) == 16
        
        # Label is whether a boundary was inserted after prev_turn
        label = 1.0 if prev_turn['is_boundary'] else 0.0
        
        samples.append({
            'text_prev': t_prev_text,
            'text_curr': t_curr_text,
            'metadata': m_vector,
            'label': label
        })
        
        if prev_turn['is_boundary']:
            active_tokens = 0.0
            
    return samples

def main():
    history_dirs = glob.glob(os.path.expanduser("~/.cache/turbostar/projects/*/history/*"))
    all_samples = []
    for h_dir in history_dirs:
        samples = extract_samples(h_dir)
        if samples:
            print(f"Extracted {len(samples)} turns from {h_dir}")
            all_samples.extend(samples)
            
    out_file = "dnn_training/dataset.json"
    with open(out_file, 'w') as f:
        json.dump(all_samples, f, indent=4)
    print(f"Successfully extracted {len(all_samples)} total samples to {out_file}")

if __name__ == '__main__':
    main()
