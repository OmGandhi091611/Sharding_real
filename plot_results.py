import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

# --- Load real experiment results ---
df_real = pd.read_csv("results.csv")
df_real = df_real[["block_size", "num_shards", "tps"]].copy()
df_real["source"] = "Real"

# --- Load simulation results (normalize column names) ---
df_sim = pd.read_csv("simulation_results.csv")
df_sim = df_sim.rename(columns={"block size": "block_size", "shards": "num_shards"})
df_sim = df_sim[["block_size", "num_shards", "tps"]].copy()
df_sim["source"] = "Simulation"

df = pd.concat([df_real, df_sim], ignore_index=True)

block_sizes = sorted(df["block_size"].unique())
shard_values = sorted(df["num_shards"].unique())

ncols = 3
nrows = (len(block_sizes) + ncols - 1) // ncols

fig, axes = plt.subplots(nrows, ncols, figsize=(18, 5 * nrows), sharey=False)
axes = axes.flatten()

sources = ["Real", "Simulation"]
colors  = {"Real": "#1f77b4", "Simulation": "#ff7f0e"}
bar_width = 0.35
x = np.arange(len(shard_values))

for i, bs in enumerate(block_sizes):
    ax = axes[i]
    for j, source in enumerate(sources):
        subset = df[(df["block_size"] == bs) & (df["source"] == source)].sort_values("num_shards")
        if subset.empty:
            continue
        offset = (j - 0.5) * bar_width
        ax.bar(x + offset, subset["tps"].values, width=bar_width,
               color=colors[source], label=source, edgecolor="white", linewidth=0.4)

    ax.set_title(f"Block Size {bs:,}", fontsize=11, fontweight="bold")
    ax.set_xlabel("Number of Shards", fontsize=10)
    ax.set_ylabel("TPS", fontsize=10)
    ax.set_xticks(x)
    ax.set_xticklabels([str(s) for s in shard_values], fontsize=9)
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda v, _: f"{v:,.0f}"))
    ax.grid(True, axis="y", linestyle="--", alpha=0.5)
    ax.legend(fontsize=9)

for j in range(i + 1, len(axes)):
    axes[j].set_visible(False)

fig.suptitle(
    "TPS vs Number of Shards — Real vs Simulation (All Block Sizes)",
    fontsize=15, fontweight="bold",
)
plt.tight_layout()
plt.savefig("tps_vs_shards_comparison.png", dpi=150, bbox_inches="tight")
print("Saved to tps_vs_shards_comparison.png")
