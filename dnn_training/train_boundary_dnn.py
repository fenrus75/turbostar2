#!/usr/bin/env python3
import os
import json
import re
import binascii
import torch
import torch.nn as nn
import torch.nn.functional as F
import torch.optim as optim

def tokenize(text):
    return re.findall(r'[a-zA-Z0-9]+', text.lower())

def hash_token(token):
    # CRC32 hash mod 1024 as requested
    return binascii.crc32(token.encode('utf-8')) % 1024

def pool_text(tokens, embed_weight):
    pooled = torch.zeros(512, device=embed_weight.device)
    if len(tokens) == 0:
        return pooled
    L = len(tokens)
    for q in range(4):
        start = (q * L) // 4
        end = ((q + 1) * L) // 4
        if end <= start:
            end = start + 1
        if end > L:
            end = L
            
        slice_tokens = tokens[start:end]
        # Get embeddings
        slice_embeds = embed_weight[slice_tokens]
        # Mean
        pooled[q*128 : (q+1)*128] = slice_embeds.mean(dim=0)
    return pooled

class MilestoneBoundaryClassifier(nn.Module):
    def __init__(self):
        super().__init__()
        self.embed = nn.Embedding(1024, 128)
        # Uniform initialization
        nn.init.uniform_(self.embed.weight, -0.1, 0.1)
        
        # Classifier layers
        self.fc1 = nn.Linear(1040, 256)
        self.fc2 = nn.Linear(256, 128)
        self.fc3 = nn.Linear(128, 64)
        self.fc4 = nn.Linear(64, 1)
        
        # Initialize dense layers
        nn.init.xavier_uniform_(self.fc1.weight)
        nn.init.zeros_(self.fc1.bias)
        nn.init.xavier_uniform_(self.fc2.weight)
        nn.init.zeros_(self.fc2.bias)
        nn.init.xavier_uniform_(self.fc3.weight)
        nn.init.zeros_(self.fc3.bias)
        nn.init.xavier_uniform_(self.fc4.weight)
        nn.init.zeros_(self.fc4.bias)
        
    def forward(self, batch_tokens_prev, batch_tokens_curr, batch_metadata):
        outputs = []
        for i in range(len(batch_tokens_prev)):
            v_prev = pool_text(batch_tokens_prev[i], self.embed.weight)
            v_curr = pool_text(batch_tokens_curr[i], self.embed.weight)
            x = torch.cat([v_prev, v_curr, batch_metadata[i]], dim=0)
            x = F.leaky_relu(self.fc1(x), negative_slope=0.01)
            x = F.leaky_relu(self.fc2(x), negative_slope=0.01)
            x = F.leaky_relu(self.fc3(x), negative_slope=0.01)
            x = torch.sigmoid(self.fc4(x))
            outputs.append(x)
        return torch.stack(outputs)

def load_dataset():
    # Prioritize LLM-labeled dataset if it exists
    dataset_file = "dnn_training/dataset_llm_labeled.json"
    if os.path.exists(dataset_file):
        print(f"Using LLM-labeled dataset: {dataset_file}")
    else:
        dataset_file = "dnn_training/dataset.json"
        if not os.path.exists(dataset_file):
            raise FileNotFoundError(f"Dataset file {dataset_file} not found. Run extract_dataset.py first.")
        print(f"Using default heuristic dataset: {dataset_file}")
        
    with open(dataset_file, 'r') as f:
        samples = json.load(f)
        
    augmented_samples = []
    for s in samples:
        # Tokenize and hash texts
        tokens_prev = [hash_token(t) for t in tokenize(s['text_prev'])]
        tokens_curr = [hash_token(t) for t in tokenize(s['text_curr'])]
        
        # Data Augmentation: Clone samples across token_pressure levels
        # token_pressure level is at indices 6, 7, 8, 9 of metadata vector M.
        for pressure_idx in range(4):
            meta_clone = list(s['metadata'])
            # Reset pressure indices to 0
            for idx in range(6, 10):
                meta_clone[idx] = 0.0
            # Set the cloned pressure level
            meta_clone[6 + pressure_idx] = 1.0
            
            augmented_samples.append({
                'tokens_prev': tokens_prev,
                'tokens_curr': tokens_curr,
                'metadata': meta_clone,
                'label': s['label']
            })
            
    return augmented_samples

def train():
    print("Loading dataset...")
    samples = load_dataset()
    print(f"Loaded {len(samples)} samples (after data augmentation).")
    
    # Check class balance
    num_pos = sum(1 for s in samples if s['label'] == 1.0)
    num_neg = len(samples) - num_pos
    print(f"Class balance: Positive (boundaries)={num_pos}, Negative={num_neg}")
    
    # Prepare PyTorch data
    batch_tokens_prev = [torch.tensor(s['tokens_prev'], dtype=torch.long) for s in samples]
    batch_tokens_curr = [torch.tensor(s['tokens_curr'], dtype=torch.long) for s in samples]
    batch_metadata = torch.tensor([s['metadata'] for s in samples], dtype=torch.float)
    batch_labels = torch.tensor([[s['label']] for s in samples], dtype=torch.float)
    
    # Model & optimizer
    model = MilestoneBoundaryClassifier()
    optimizer = optim.Adam(model.parameters(), lr=0.002, weight_decay=1e-4)
    
    # Weighted Binary Cross Entropy Loss to handle imbalance
    pos_weight = torch.tensor([float(num_neg) / max(1.0, float(num_pos))])
    criterion = nn.BCEWithLogitsLoss(pos_weight=pos_weight)
    
    # Training loop
    print("Training model...")
    model.train()
    for epoch in range(150):
        optimizer.zero_grad()
        # forward pass (logits)
        # To get logits instead of sigmoid for BCEWithLogitsLoss, we evaluate without sigmoid:
        logits = []
        for i in range(len(samples)):
            v_prev = pool_text(batch_tokens_prev[i], model.embed.weight)
            v_curr = pool_text(batch_tokens_curr[i], model.embed.weight)
            x = torch.cat([v_prev, v_curr, batch_metadata[i]], dim=0)
            x = F.leaky_relu(model.fc1(x), negative_slope=0.01)
            x = F.leaky_relu(model.fc2(x), negative_slope=0.01)
            x = F.leaky_relu(model.fc3(x), negative_slope=0.01)
            logit = model.fc4(x)
            logits.append(logit)
        logits = torch.stack(logits)
        
        loss = criterion(logits, batch_labels)
        loss.backward()
        optimizer.step()
        
        if (epoch + 1) % 15 == 0 or epoch == 0:
            # Calculate accuracy
            with torch.no_grad():
                probs = torch.sigmoid(logits)
                preds = (probs > 0.5).float()
                correct = (preds == batch_labels).float().sum().item()
                acc = correct / len(samples)
                
                # Precision and Recall
                tp = ((preds == 1.0) & (batch_labels == 1.0)).float().sum().item()
                fp = ((preds == 1.0) & (batch_labels == 0.0)).float().sum().item()
                fn = ((preds == 0.0) & (batch_labels == 1.0)).float().sum().item()
                precision = tp / (tp + fp) if (tp + fp) > 0 else 0.0
                recall = tp / (tp + fn) if (tp + fn) > 0 else 0.0
                
            print(f"Epoch {epoch+1:03d} | Loss: {loss.item():.4f} | Acc: {acc:.4f} | Precision: {precision:.4f} | Recall: {recall:.4f}")
            
    # Save weights.json
    print("Saving weights to weights.json...")
    weights_json = {
        'embedding_matrix': model.embed.weight.detach().cpu().numpy().tolist(),
        'fc1_weight': model.fc1.weight.detach().cpu().numpy().tolist(),
        'fc1_bias': model.fc1.bias.detach().cpu().numpy().tolist(),
        'fc2_weight': model.fc2.weight.detach().cpu().numpy().tolist(),
        'fc2_bias': model.fc2.bias.detach().cpu().numpy().tolist(),
        'fc3_weight': model.fc3.weight.detach().cpu().numpy().tolist(),
        'fc3_bias': model.fc3.bias.detach().cpu().numpy().tolist(),
        'fc4_weight': model.fc4.weight.detach().cpu().numpy().tolist(),
        'fc4_bias': model.fc4.bias.detach().cpu().numpy().tolist(),
    }
    
    out_path = "dnn_training/weights.json"
    with open(out_path, 'w') as f:
        json.dump(weights_json, f, indent=4)
    print(f"Weights successfully saved to {out_path}")

if __name__ == '__main__':
    train()
