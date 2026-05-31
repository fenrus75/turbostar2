# Local DNN Context Management Architecture

This document specifies the design for a lightweight, local Deep Neural Network (DNN) used within the TurboStar editor to optimize conversation history, memory budget compaction, and milestone boundary detection.

---

## 1. Core Use Cases

### Case 1: Context Page-In Tournament ("Is A more important than B?")
When the active conversation exceeds the target token budget, historical segments (episodes or subepisodes) are paged out to disk. When the agent requests to page-in context or when the editor needs to make partial page-in decisions, the DNN acts as a referee to select the most relevant historical context.
*   **Architecture:** Siamese Neural Network.
*   **Function:** Estimates which of two context blocks ($A$ or $B$) is more important given the current task description.

### Case 2: Milestone Boundary Detection ("Should we insert a milestone?")
As the user communicates with the agent, the editor needs to decide when to automatically group the preceding turns into a milestone, archive them, and page them out to keep the active window responsive.
*   **Architecture:** Concatenation Classification Network.
*   **Function:** Classifies whether the transition from the previous turn to the current prompt marks a new episode boundary.

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

### Tokenization & Feature Hashing
To maintain a zero-dependency local C++ runtime without loading large vocabulary dictionaries:
*   Words from the text inputs are tokenized by whitespace and punctuation.
*   Tokens are hashed into a fixed vocabulary index space ($V = 1024$) using a fast hashing function (e.g., MurmurHash3 or `std::hash`).
*   Hashed indices map directly to a learned $128$-dimensional token embedding matrix ($W_{\text{embed}} \in \mathbb{R}^{1024 \times 128}$).

### Regional Pooling (Sequence Encoding)
To capture basic chronological structure without the complexity of Recurrent or Transformer layers, tokens in a text block are mapped into 4 distinct pooling slots (producing a $4 \times 128 = 512$-dimensional vector):
1.  **Slot 0 (Global Mean):** Mean embedding of all tokens.
2.  **Slot 1 (Beginning):** Mean embedding of tokens in the first 25% of the sequence.
3.  **Slot 2 (Middle):** Mean embedding of tokens in the middle 50% of the sequence.
4.  **Slot 3 (End):** Mean embedding of tokens in the last 25% of the sequence.

---

## 3. Discretized Metadata Feature Space (Case 2)

Continuous values (time, token counts) are discretized into one-hot encoded bins to prevent scale-saturation and ensure training stability. A $13$-dimensional metadata vector $M$ is concatenated to the pooled prompt vectors:

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

---

## 4. Network Topologies

### Case 1: Siamese Score Predictor
For two candidates $A$ and $B$, each is passed through a shared Multi-Layer Perceptron (MLP) to output a single scalar score:
$$\text{Score} = \text{MLP}(\text{Vector}) \in \mathbb{R}$$
$$\text{Prob}(A > B) = \sigma(\text{Score}_A - \text{Score}_B)$$
*   **MLP Layer Sizes:** $512$ (Pooled Text) $\rightarrow 128 \rightarrow 64 \rightarrow 1$ (Score).
*   **Activation:** LeakyReLU (slope $0.01$) for hidden layers, linear mapping for the final score.

### Case 2: Classifier
$$\text{Input} = [ V_{T-1} \,\|\, V_T \,\|\, M ] \in \mathbb{R}^{1037}$$
$$\text{Prob}(\text{Boundary}) = \sigma(\text{MLP}(\text{Input}))$$
*   **MLP Layer Sizes:** $1037 \rightarrow 256 \rightarrow 128 \rightarrow 64 \rightarrow 1$.
*   **Activation:** LeakyReLU for hidden layers, Sigmoid ($\sigma$) for final probability.

---

## 5. Training & Serialization Workflow

The design leverages a **hybrid training loop** where heavy training happens offline or via a separate Python tool, while TUI inference is native, fast, and dependency-free.

```
[ Turbostar C++ TUI ]
       │
       ├─► Inference: Load weights.json, run forward-pass loops (LeakyReLU)
       │
       └─► Logging: Output text buffers & metrics to raw_dataset.json
             │
             ▼
[ Offline / Sidecar Python App ]
       │
       ├─► Data Augmentation: Perturb token_pressure levels (Faking Pressure)
       ├─► Training: PyTorch (GPU) optimizing binary cross-entropy loss
       │
       ▼
  weights.json (weights, biases, & embedding matrices serialized to JSON)
```

### Data Augmentation (Faking Pressure)
To ensure the model learns to force milestones when context limits are reached:
1.  During training data prep, each sample is cloned 4 times—once for each `token_pressure` level.
2.  If the original target is `No Milestone` but the cloned sample's pressure is set to `Critical`, the training target is flipped to `Milestone`. This teaches the network that high token pressure overrides marginal semantic continuations.
