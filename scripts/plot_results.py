import pandas as pd
import matplotlib.pyplot as plt
import os

df = pd.read_csv('perf.csv')

plt.figure(figsize=(10, 6))
plt.plot(df['N'], df['InsertTime'], marker='o', linewidth=2, color='#3498db', label='Insertion (total)')
plt.plot(df['N'], df['SearchTime'], marker='s', linewidth=2, color='#2ecc71', label='Search (scaled to N ops)')
plt.plot(df['N'], df['RangeTime'], marker='^', linewidth=2, color='#e74c3c', label='Range Query (2000 ops)')

plt.title('HydroDS Performance Profiling', fontsize=16, fontweight='bold')
plt.xlabel('Number of Elements (N)', fontsize=12)
plt.ylabel('Time (Seconds)', fontsize=12)
plt.grid(True, linestyle='--', alpha=0.7)
plt.legend(fontsize=12)
plt.tight_layout()

# Save in artifact directory
artifact_dir = '/home/anurag-panwar/.gemini/antigravity-cli/brain/cb684a0d-7ea2-4a3a-a867-7d889e03d372'
os.makedirs(artifact_dir, exist_ok=True)
plt.savefig(os.path.join(artifact_dir, 'hydrods_perf.png'), dpi=300)
print("Plot saved successfully.")
