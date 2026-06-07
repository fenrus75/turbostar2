# Local DNN Context Management Architecture

This document specifies the design for a lightweight, local Deep Neural Network (DNN) used within the TurboStar editor to optimize conversation history, memory budget compaction, and milestone boundary detection.

To maximize execution efficiency on CPU, all layer sizes, embedding dimensions, and input/output tensors are strictly constrained to be **multiples of 16** to allow compilers to optimize for SIMD vectorization (AVX, AVX2, or SSE instructions).

---

## 1. Core Use Cases

### Case 1: Context Page-In Tournament ("Is A more important than B?")
When the active conversation exceeds the target token budget, historical segments (episodes or subepisodes) are paged out to disk. When the editor needs to make partial page-in decisions, the DNN acts as a referee to select the most relevant historical context.
*   **Architecture:** Siamese Neural Network.
*   **Function:** Estimates which of two context blocks ($A$ or $B$) is more important given the current task description.

### Case 2: Milestone Boundary Detection ("Should we insert a milestone?")
As the user communicates with the agent, the editor needs to decide when to automatically group the preceding turns into a milestone, archive them, and page them out to keep the active window responsive.
*   **Architecture:** Concatenation Classification Network.
*   **Function:** Classifies whether the transition from the previous turn to the current prompt marks a new episode boundary.

### Case 3: Context Auto-Page In ("Should we automatically restore past context?")
When the user types a new prompt, the system scans the indices, tags, and demand-load hints of all paged-out milestones. The DNN determines if any archived milestone is highly relevant to the new user prompt to automatically restore it before LLM invocation.
*   **Architecture:** Dual-Encoder Matcher / Relevance Classifier.
*   **Function:** Evaluates the semantic relevance between the current prompt $T$ and the candidate milestone summary/hint $S$ to decide if a proactive page-in should be triggered.

---

## 2. Input Representation & Feature Engineering

To prevent massive compiler outputs, raw file reads, or tool execution logs from diluting the semantic signal, input text is strictly filtered before tokenization.

### Text Inputs and Noise Filtering
*   **Current User Prompt ($T$):** The raw text of the new user prompt.
*   **Previous Turn Context ($T-1$):** Omit intermediate tool calls, terminal stdout, and file contents. Instead, construct it as:
    ```
    [Previous User Prompt] + " [Agent Conclusion: ] " + [Last 50 words of Agent's Response]
    ```
*   **Task Description:** The active overarching project task/goal description.

### Pro/Con Analysis: Whole Episode vs. Demand-Load Hint for Auto-Page In

For Case 3 (Auto-Page In), we must decide whether the DNN should match the user prompt against the **whole text** of the paged-out episode or against the **demand-load hint** (the 1-2 sentence "when to page back in" description).

| Approach | Pros | Cons |
| :--- | :--- | :--- |
| **Whole Episode Text** | • **High Granularity:** Captures specific C++ class names, function signatures, variables, and file paths not mentioned in the summary.<br>• **LLM-Independent:** Does not fail if the LLM generates a poor or generic summary. | • **High Noise-to-Signal Ratio:** Diff hunks, compiler logs, and system prompt text dilute the semantic signal.<br>• **Hash Collisions:** Large files saturate the $V=1024$ hash space, causing high collision rates.<br>• **CPU Overhead:** Large inputs require tokenizing and pooling thousands of words on every prompt, degrading UI responsiveness. |
| **Short Description (Demand-Load Hint)** | • **Extremely Focused:** The agent writes the hint specifically to define when to load the block, maximizing signal-to-noise.<br>• **Low Hash Collisions:** Small text lengths minimize hashing collisions.<br>• **High Speed:** Tokenizing and pooling a few dozen words is extremely fast ($<1$ ms), preserving TUI responsiveness. | • **Symbol Amnesia:** Specific code symbols or variables not in the summary cannot be matched.<br>• **LLM Dependency:** Relies on the agent writing a high-quality, descriptive demand-load hint. |

**Architectural Decision:** Turbostar implements the **Short Description (Demand-Load Hint) augmented with Tags** as the primary input representation for the candidate episode. This maximizes performance and minimizes TUI latency. To mitigate the "Symbol Amnesia" con, the system automatically appends the list of modified filenames and active tags to the hint text before tokenization.

### Tokenization & Feature Hashing
To maintain a zero-dependency local C++ runtime without loading large vocabulary dictionaries:
*   Words from the text inputs are tokenized by whitespace and punctuation.
*   Tokens are hashed into a fixed vocabulary index space ($V = 1024$) using a fast hashing function (e.g., MurmurHash3 or `std::hash`).
*   Hashed indices map directly to a learned $128$-dimensional token embedding matrix ($W_{\text{embed}} \in \mathbb{R}^{1024 \times 128}$).

### 25% Equal-Split Regional Pooling
To capture chronological sequence structure, tokens in a text block are mapped into exactly 4 equal, contiguous quarters (producing a $4 \times 128 = 512$-dimensional vector):
1.  **Slot 0 (Quarter 1):** Mean embedding of all tokens in range $[0 \dots \lfloor L/4 \rfloor]$.
2.  **Slot 1 (Quarter 2):** Mean embedding of all tokens in range $[\lfloor L/4 \rfloor \dots \lfloor L/2 \rfloor]$.
3.  **Slot 2 (Quarter 3):** Mean embedding of all tokens in range $[\lfloor L/2 \dots \lfloor 3L/4 \rfloor]$.
4.  **Slot 3 (Quarter 4):** Mean embedding of all tokens in range $[\lfloor 3L/4 \rfloor \dots L]$.

---

## 3. Discretized Metadata Feature Space (Case 2)

Continuous values (time, token counts) are discretized into one-hot encoded bins to prevent scale-saturation and ensure training stability. 

To satisfy the **multiple of 16** tensor requirement for CPU SIMD optimization, the metadata vector $M$ contains 13 features padded with 3 zero-values to yield a final shape of **16 dimensions**:

*   **`idle_to_prompt` Time Gap (3-dim one-hot):**
    *   `[1, 0, 0]`: Short ($< 1$ minute)
    *   `[0, 1, 0]`: Medium ($1$ to $5$ minutes)
    *   `[0, 0, 1]`: Long ($\ge 5$ minutes)
*   **`agent_thinking_time` Duration (3-dim one-hot):**
    *   `[1, 0, 0]`: Short ($< 10$ seconds)
    *   `[0, 1, 0]`: Medium ($10$ seconds to $2$ minutes)
    *   `[0, 0, 1]`: Long ($\ge 2$ minutes)
*   **`token_pressure` Level (4-dim one-hot):**
    *   `[1, 0, 0, 0]`: Normal ($< 60\%$ of target budget)
    *   `[0, 1, 0, 0]`: Warning ($60\%$ to $80\%$)
    *   `[0, 0, 1, 0]`: High ($80\%$ to $95\%$)
    *   `[0, 0, 0, 1]`: Critical ($\ge 95\%$)
*   **`deterministic_flags` (3-dim binary):**
    *   `[git_commit_executed, compile_succeeded, tests_passed]`
*   **`padding` (3-dim zero-padding):**
    *   `[0, 0, 0]`

---

## 4. Network Topologies

### Case 1: Siamese Score Predictor
For two candidates $A$ and $B$, each is passed through a shared Multi-Layer Perceptron (MLP) to output a single scalar score:
$$\text{Score} = \text{MLP}(\text{Vector}) \in \mathbb{R}$$
$$\text{Prob}(A > B) = \sigma(\text{Score}_A - \text{Score}_B)$$
*   **MLP Layer Sizes:** $512$ (Pooled Text) $\rightarrow 128 \rightarrow 64 \rightarrow 1$ (Score).
*   **Activation:** LeakyReLU (slope $0.01$) for hidden layers, linear mapping for the final score.

### Case 2: Classifier
$$\text{Input} = [ V_{T-1} \,\|\, V_T \,\|\, M ] \in \mathbb{R}^{1040}$$
$$\text{Prob}(\text{Boundary}) = \text{softmax}(\text{MLP}(\text{Input}))[0]$$
*   **MLP Layer Sizes:** $1040 \rightarrow 256 \rightarrow 128 \rightarrow 64 \rightarrow 2$.
*   **Activation & Regularization:** LeakyReLU for hidden layers, Dropout (prob $0.1$) during training to prevent overfitting, and Softmax for the final outputs. The first element corresponds to "new episode" (boundary) and the second element to "no new episode".

### Case 3: Relevance Matcher
$$\text{Input} = [ V_T \,\|\, V_S \,\|\, |V_T - V_S| \,\|\, V_T \odot V_S ] \in \mathbb{R}^{2048}$$
$$\text{Prob}(\text{Relevant}) = \sigma(\text{MLP}(\text{Input}))$$
*   **MLP Layer Sizes:** $2048 \rightarrow 512 \rightarrow 128 \rightarrow 64 \rightarrow 1$ (Score).
*   **Activation:** LeakyReLU (slope $0.01$) for hidden layers, Sigmoid activation for the final relevance score.

---

## 5. Decision Thresholds (Business-Model Driven)

The probability threshold used to trigger a milestone split is dynamically derived from the model's cost classification:
*   **Free / Local Inference Models:** Set threshold to **`0.35`** to prioritize smaller active contexts and lower execution latencies.
*   **Paid / Token-Based API Models:** Set threshold to **`0.65`** to favor larger context retention and maximize prompt caching prefix matches.

### Case 3 (Auto-Page In) Relevance Thresholds
The relevance threshold for Auto-Page In is dynamically scaled based on active token pressure to prevent context window bloat:
*   **Normal Pressure (< 60% of target budget):** Set threshold to **`0.50`** (permissive page-in, restoring context using Level 1 "think-free" compression).
*   **Warning / High Pressure (60% to 95% of target budget):** Set threshold to **`0.75`** (restrictive page-in, restoring context using Level 2/3 aggressive compression).
*   **Critical Pressure (>= 95% of target budget):** Auto-page-in is disabled (effective threshold of **`1.00`**) to protect the model's active token budget.

---

## 6. Training & Serialization Workflow

The design leverages a **hybrid training loop** where heavy training happens offline or via a separate Python tool, while TUI inference is native, fast, and dependency-free.

```
[ Turbostar C++ TUI ]
       │
       ├─► Inference: Load weights.json, run forward-pass loops (LeakyReLU)
       │
       └─► Logging: Conversation logs contain Unix timestamps & durations on turns
             │
             ▼
[ Offline / Sidecar Python App ]
       │
       ├─► Extraction: extract_dataset.py extracts raw features & turn history
       │
       ├─► LLM-as-a-judge (Optional): label_with_local_llm.py queries a local
       │   LLM to refine target milestone boundaries with clean reasoning.
       │   For our local environment, run this script using:
       │   `python3 dnn_training/label_with_local_llm.py --api-url http://192.168.1.55:8080/v1/chat/completions --model Qwen/Qwen3-Coder-Next`
       │
       ├─► Data Augmentation: Clone samples across token_pressure levels
       ├─► Training: PyTorch optimizing weighted binary cross-entropy loss
       │
       ▼
  weights.json (weights, biases, & embedding matrices serialized to JSON)
```

---

## 7. C++ Forward-Pass Implementation Details

To ensure runtime efficiency and eliminate compiler overheads, the C++ forward pass implements dense layers as direct vector dot-products.

### Fast Hashing Trick
Words are tokenized by space and mapped to a $0..1023$ hash space:
```cpp
size_t hash_token(const std::string& token) {
    std::hash<std::string> hasher;
    return hasher(token) % 1024;
}
```

### Region Pooling Execution
```cpp
std::vector<float> pool_text(const std::string& text, const std::vector<std::vector<float>>& embed_matrix) {
    std::vector<std::string> tokens = tokenize(text);
    std::vector<float> pooled(512, 0.0f);
    if (tokens.empty()) return pooled;

    size_t L = tokens.size();
    for (int q = 0; q < 4; ++q) {
        size_t start = (q * L) / 4;
        size_t end = ((q + 1) * L) / 4;
        if (end <= start) end = start + 1;
        if (end > L) end = L;

        std::vector<float> sum(128, 0.0f);
        for (size_t i = start; i < end; ++i) {
            size_t h = hash_token(tokens[i]);
            const auto& emb = embed_matrix[h];
            for (int d = 0; d < 128; ++d) sum[d] += emb[d];
        }
        float count = static_cast<float>(end - start);
        for (int d = 0; d < 128; ++d) {
            pooled[q * 128 + d] = sum[d] / count;
        }
    }
    return pooled;
}
```

### Layer Forward-Pass with LeakyReLU
The dot-product loop utilizes LeakyReLU with a slope of `0.01` to prevent dying neurons:
```cpp
std::vector<float> evaluate_layer(const std::vector<float>& input, 
                                  const std::vector<std::vector<float>>& weights, 
                                  const std::vector<float>& biases) {
    std::vector<float> output(weights.size());
    for (size_t i = 0; i < weights.size(); ++i) {
        float sum = biases[i];
        // Compilers vectorize this inner loop automatically when sizes are multiples of 16
        for (size_t j = 0; j < input.size(); ++j) {
            sum += input[j] * weights[i][j];
        }
        output[i] = sum > 0.0f ? sum : 0.01f * sum;
    }
    return output;
}
```

---

## 8. Dataset Extraction & Training Walkthrough

Follow these steps to extract logged conversation histories, refine milestone labels via LLM-as-a-judge, train the classifier, and verify the C++ model.

### Step 1: Extract the Raw Heuristic Dataset
Extract conversation turns and initial boundaries from the active logged history:
```bash
python3 dnn_training/extract_dataset.py
```
This parses logs under `~/.cache/turbostar/projects/*/history/*` and writes output to `dnn_training/dataset.json`.

### Step 2: Run LLM-as-a-Judge Labeling (with Caching and Concurrency)
Refine transitions using the local network or local LLM endpoint. To speed up the process, use the parallelized labeling script:
```bash
python3 dnn_training/label_with_local_llm_fast.py \
    --api-url http://192.168.1.55:8080/v1/chat/completions \
    --model Qwen/Qwen3-Coder-Next \
    --threads 5
```
*   **Concurrency Guidelines:** Querying local LLM servers (like llama.cpp, vLLM, or Ollama) with too many concurrent threads (e.g. `--threads 16`) will overload their parallel slots, causing immediate connection drops or HTTP 503 rejections. Restricting concurrency to match the server's parallel throughput (e.g., `--threads 4` or `--threads 5`) yields 100% query success and a 4-5x speedup over sequential processing.
*   **Caching & Resiliency:** The script loads previously labeled samples from `dnn_training/dataset_llm_labeled.json` to avoid duplicate work. Progress is written back to disk dynamically, making it safe to interrupt and resume.

### Step 3: Train the Classifier Model
Train the milestone boundary classification model and export optimized C++ weights:
```bash
./dnn_training/train_boundary_dnn.py
```
*   This trains the PyTorch model using data augmentation (cloning across pressure levels) and writes the weights, biases, and embedding matrices to `dnn_training/weights.json`.
*   **Early Stopping:** To prevent overfitting and avoid unnecessary training epochs once the loss converges, the script implements an early stopping trigger when the training loss falls below `0.001`.

### Step 4: Verify C++ Inference Match
Re-compile the C++ codebase and run the unit tests to confirm the newly trained weights are correctly parsed and evaluated:
```bash
ninja -C build && ./build/test_context_dnn
```
If successful, it prints `Milestone Boundary C++ DNN unit tests passed successfully!`.
