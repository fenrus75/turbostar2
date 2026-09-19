# Context DNN Boundary Predictor (`context_dnn`)

## Goals
The `context_dnn` class provides an ultra-fast, local neural network inference engine that evaluates conversational turn transitions to detect semantic episode boundaries. This enables Turbostar to automatically identify logical milestones and partition conversation history into episodes without invoking expensive external LLM summarizers.

## Architecture and Constraints
- **Zero-Dependency Forward Pass**: Implements a lightweight 4-layer fully connected multi-layer perceptron (MLP) purely in C++ without external ML runtime dependencies (e.g. PyTorch or ONNX).
- **Feature Pipeline**:
  - Tokenizes turn $T-1$ and turn $T$ texts.
  - Generates token embeddings using a CRC32 hash trick into a compact embedding matrix.
  - Pools embeddings (mean pooling) and concatenates with a 16-dimensional metadata feature vector.
  - Evaluates dense layers with ReLU activations and a final sigmoid unit to output boundary probability $[0.0, 1.0]$.
- **Memory-Mapped Weights**: Uses `mmap` to load binary weight files (`dnn_weights`) with zero copy and minimal heap allocations.

## Lessons Learned
- **Deterministic Hashing**: Using CRC32 hashing for vocabulary index projection provides consistent token embedding lookups across architectures while keeping binary weight sizes small.
- **Microsecond Latency**: Running neural evaluation on memory-mapped weights executes in under 100 microseconds, allowing real-time boundary checks after every assistant turn without slowing the agent loop.
