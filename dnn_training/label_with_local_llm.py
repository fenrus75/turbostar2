#!/usr/bin/env python3
import os
import sys
import json
import argparse
import urllib.request
import urllib.error

def query_local_llm(api_url, api_key, model, prompt):
    headers = {
        "Content-Type": "application/json",
    }
    if api_key:
        headers["Authorization"] = f"Bearer {api_key}"
        
    payload = {
        "model": model,
        "messages": [
            {
                "role": "system",
                "content": "You are a system context manager helper. You decide if a task milestone boundary has been reached. You must respond ONLY with a JSON object."
            },
            {
                "role": "user",
                "content": prompt
            }
        ],
        "temperature": 0.1,
        "response_format": {"type": "json_object"}
    }
    
    req = urllib.request.Request(
        api_url, 
        data=json.dumps(payload).encode("utf-8"), 
        headers=headers, 
        method="POST"
    )
    
    try:
        with urllib.request.urlopen(req, timeout=30) as response:
            res_data = json.loads(response.read().decode("utf-8"))
            content = res_data["choices"][0]["message"]["content"]
            return content
    except urllib.error.URLError as e:
        print(f"Connection error to LLM API: {e}", file=sys.stderr)
        return None
    except Exception as e:
        print(f"Error querying LLM API: {e}", file=sys.stderr)
        return None

def main():
    parser = argparse.ArgumentParser(description="Label conversation dataset transitions using a local LLM as a judge.")
    parser.add_argument("--api-url", default="http://localhost:11434/v1/chat/completions", help="OpenAI-compatible chat completion URL (default: Ollama's local v1 completions)")
    parser.add_argument("--api-key", default="sk-no-key-required", help="API Key if required by endpoint")
    parser.add_argument("--model", default="llama3", help="Model name (default: llama3)")
    parser.add_argument("--input", default="dnn_training/dataset.json", help="Input dataset path")
    parser.add_argument("--output", default="dnn_training/dataset_llm_labeled.json", help="Output labeled dataset path")
    args = parser.parse_args()
    
    if not os.path.exists(args.input):
        print(f"Error: Input dataset {args.input} does not exist. Run extract_dataset.py first.")
        sys.exit(1)
        
    with open(args.input, "r") as f:
        samples = json.load(f)
        
    print(f"Loaded {len(samples)} samples from {args.input}")
    print(f"Targeting local LLM endpoint: {args.api_url} using model: {args.model}")
    
    labeled_samples = []
    successful_calls = 0
    
    for i, s in enumerate(samples):
        # Decode deterministic flags from metadata
        # M = idle_onehot(3) + think_onehot(3) + pressure_onehot(4) + flags(3) + padding(3)
        git_commit = bool(s["metadata"][10])
        compile_ok = bool(s["metadata"][11])
        test_ok = bool(s["metadata"][12])
        
        prompt = f"""Evaluate if a "Milestone Boundary" has been reached between the following two conversation turns.

A Milestone Boundary is a point where a task, sub-task, or phase of work is complete, and it is safe/logical to "page out" or archive the preceding conversation history to keep the active context window clean and small.

Look at the transition from Turn T-1 to Turn T:

[Turn T-1 Context (Prompt + last part of Agent response)]:
{s["text_prev"]}

[Turn T User Prompt]:
{s["text_curr"]}

[Metadata & Action Flags from T-1]:
- Git Commit Executed: {git_commit}
- Compilation Succeeded: {compile_ok}
- Tests Passed: {test_ok}

Evaluate if the transition between T-1 and T represents a clean breakpoint (e.g. after a successful compile/test/commit cycle, or when switching to a completely new task or starting a fresh instruction).

You must respond in valid JSON format with the following keys:
{{
    "reasoning": "A brief explanation of why this transition is or is not a milestone boundary.",
    "is_boundary": true or false
}}
"""
        print(f"Processing sample {i+1}/{len(samples)}... ", end="", flush=True)
        response_str = query_local_llm(args.api_url, args.api_key, args.model, prompt)
        
        label = s["label"]  # Default to heuristic label
        reasoning = "LLM labeling failed or was skipped"
        
        if response_str:
            try:
                # Clean code blocks if returned
                if response_str.strip().startswith("```"):
                    # Extract contents of code block
                    lines = response_str.strip().split("\n")
                    if lines[0].startswith("```json") or lines[0].startswith("```"):
                        lines = lines[1:-1]
                    response_str = "\n".join(lines)
                
                resp_json = json.loads(response_str)
                is_boundary = resp_json.get("is_boundary")
                reasoning = resp_json.get("reasoning", "")
                
                if isinstance(is_boundary, bool):
                    label = 1.0 if is_boundary else 0.0
                    successful_calls += 1
                    print(f"Success! Label: {label} (Reasoning: {reasoning[:60]}...)")
                else:
                    print(f"Warning: 'is_boundary' not a boolean. Falling back to heuristic.")
            except Exception as e:
                print(f"Warning: Failed to parse JSON response: {e}. Falling back to heuristic.")
                print(f"Raw Response: {response_str}")
        else:
            print("Failed. Falling back to heuristic.")
            
        s_labeled = s.copy()
        s_labeled["label"] = label
        s_labeled["llm_reasoning"] = reasoning
        labeled_samples.append(s_labeled)
        
    with open(args.output, "w") as f:
        json.dump(labeled_samples, f, indent=4)
        
    print(f"\nFinished LLM labeling. Labeled {successful_calls}/{len(samples)} samples successfully.")
    print(f"Saved LLM-labeled dataset to {args.output}")

if __name__ == "__main__":
    main()
