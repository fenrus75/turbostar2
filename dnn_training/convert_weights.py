import json
import struct
import os

def convert():
    json_path = "dnn_training/weights.json"
    bin_path = "dnn_training/weights.bin"
    
    if not os.path.exists(json_path):
        json_path = "../dnn_training/weights.json"
        bin_path = "../dnn_training/weights.bin"
        
    print(f"Loading {json_path}...")
    with open(json_path, 'r') as f:
        data = json.load(f)
        
    print(f"Writing {bin_path}...")
    with open(bin_path, 'wb') as f:
        # Header
        magic = b'TSDNNWGT'
        version = 1
        embed_rows = 1024
        embed_cols = 128
        fc1_out = 256
        fc1_in = 1040
        fc2_out = 128
        fc2_in = 256
        fc3_out = 64
        fc3_in = 128
        fc4_out = 2
        fc4_in = 64
        
        header = struct.pack('<8sIIIIIIIIIIII', magic, version, embed_rows, embed_cols, fc1_out, fc1_in, fc2_out, fc2_in, fc3_out, fc3_in, fc4_out, fc4_in, 0)
        f.write(header)
        
        # Flatten and write float arrays
        tensors = [
            'embedding_matrix',
            'fc1_weight',
            'fc1_bias',
            'fc2_weight',
            'fc2_bias',
            'fc3_weight',
            'fc3_bias',
            'fc4_weight',
            'fc4_bias'
        ]
        
        for name in tensors:
            lst = data[name]
            # Flatten lst
            flat = []
            def flatten(l):
                if isinstance(l, list):
                    for x in l:
                        flatten(x)
                else:
                    flat.append(l)
            flatten(lst)
            print(f"Tensor {name}: writing {len(flat)} floats")
            f.write(struct.pack(f'<{len(flat)}f', *flat))
            
    print("Conversion completed successfully!")

if __name__ == "__main__":
    convert()
