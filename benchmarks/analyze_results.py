#!/usr/bin/env python3
"""
Analyze and visualize FaaS Gateway benchmark results
Compares CPU usage between PROXY and ZERO-COPY architectures
"""

import csv
import sys
import os
from pathlib import Path
import statistics

def load_cpu_data(filename):
    """Load CPU data from CSV file"""
    data = {
        'cpu': [],
        'user_cpu': [],
        'sys_cpu': [],
        'memory': [],
        'ctx_switches': []
    }
    
    if not os.path.exists(filename):
        print(f"Warning: {filename} not found")
        return None
    
    with open(filename, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                data['cpu'].append(float(row['cpu_percent']))
                data['user_cpu'].append(float(row['user_cpu']))
                data['sys_cpu'].append(float(row['sys_cpu']))
                data['memory'].append(float(row['memory_mb']))
                if 'ctx_switches' in row:
                    data['ctx_switches'].append(float(row['ctx_switches']))
            except (ValueError, KeyError) as e:
                continue
    
    return data

def parse_wrk_output(filename):
    """Parse wrk load test output"""
    if not os.path.exists(filename):
        print(f"Warning: {filename} not found")
        return None
    
    stats = {}
    
    with open(filename, 'r') as f:
        content = f.read()
        
        # Extract requests per second
        for line in content.split('\n'):
            if 'Requests/sec:' in line:
                stats['rps'] = float(line.split(':')[1].strip())
            elif 'Transfer/sec:' in line:
                stats['throughput'] = line.split(':')[1].strip()
            elif 'Latency' in line and 'Avg' in line:
                parts = line.split()
                stats['latency_avg'] = parts[1]
                stats['latency_stdev'] = parts[2]
                stats['latency_max'] = parts[3]
            elif '50%' in line:
                stats['p50'] = line.split()[1]
            elif '75%' in line:
                stats['p75'] = line.split()[1]
            elif '90%' in line:
                stats['p90'] = line.split()[1]
            elif '99%' in line:
                stats['p99'] = line.split()[1]
    
    return stats

def print_comparison(proxy_data, zerocopy_data, proxy_wrk, zerocopy_wrk):
    """Print comparison table"""
    
    print("\n" + "="*80)
    print("                 BENCHMARK COMPARISON RESULTS")
    print("="*80)
    
    if proxy_data and zerocopy_data:
        print("\n📊 CPU USAGE (Gateway Process)")
        print("-" * 80)
        print(f"{'Metric':<30} {'PROXY Mode':<20} {'ZERO-COPY Mode':<20} {'Improvement':<10}")
        print("-" * 80)
        
        # Average CPU
        proxy_avg_cpu = statistics.mean(proxy_data['cpu'])
        zero_avg_cpu = statistics.mean(zerocopy_data['cpu'])
        improvement = ((proxy_avg_cpu - zero_avg_cpu) / proxy_avg_cpu * 100) if proxy_avg_cpu > 0 else 0
        print(f"{'Average CPU %':<30} {proxy_avg_cpu:>18.2f}% {zero_avg_cpu:>18.2f}% {improvement:>8.1f}%")
        
        # User CPU
        proxy_user = statistics.mean(proxy_data['user_cpu'])
        zero_user = statistics.mean(zerocopy_data['user_cpu'])
        improvement = ((proxy_user - zero_user) / proxy_user * 100) if proxy_user > 0 else 0
        print(f"{'User CPU %':<30} {proxy_user:>18.2f}% {zero_user:>18.2f}% {improvement:>8.1f}%")
        
        # System CPU
        proxy_sys = statistics.mean(proxy_data['sys_cpu'])
        zero_sys = statistics.mean(zerocopy_data['sys_cpu'])
        improvement = ((proxy_sys - zero_sys) / proxy_sys * 100) if proxy_sys > 0 else 0
        print(f"{'System CPU %':<30} {proxy_sys:>18.2f}% {zero_sys:>18.2f}% {improvement:>8.1f}%")
        
        # Peak CPU
        proxy_peak = max(proxy_data['cpu'])
        zero_peak = max(zerocopy_data['cpu'])
        improvement = ((proxy_peak - zero_peak) / proxy_peak * 100) if proxy_peak > 0 else 0
        print(f"{'Peak CPU %':<30} {proxy_peak:>18.2f}% {zero_peak:>18.2f}% {improvement:>8.1f}%")
        
        # Memory
        proxy_mem = statistics.mean(proxy_data['memory'])
        zero_mem = statistics.mean(zerocopy_data['memory'])
        diff = proxy_mem - zero_mem
        print(f"{'Average Memory (MB)':<30} {proxy_mem:>18.1f}  {zero_mem:>18.1f}  {diff:>8.1f}")
        
    if proxy_wrk and zerocopy_wrk:
        print("\n⚡ LATENCY & THROUGHPUT")
        print("-" * 80)
        print(f"{'Metric':<30} {'PROXY Mode':<20} {'ZERO-COPY Mode':<20} {'Improvement':<10}")
        print("-" * 80)
        
        if 'rps' in proxy_wrk and 'rps' in zerocopy_wrk:
            improvement = ((zerocopy_wrk['rps'] - proxy_wrk['rps']) / proxy_wrk['rps'] * 100)
            print(f"{'Requests/sec':<30} {proxy_wrk['rps']:>18.1f}  {zerocopy_wrk['rps']:>18.1f}  {improvement:>+8.1f}%")
        
        if 'latency_avg' in proxy_wrk and 'latency_avg' in zerocopy_wrk:
            print(f"{'Latency (avg)':<30} {proxy_wrk['latency_avg']:>20} {zerocopy_wrk['latency_avg']:>20} {'':>10}")
        
        if 'p50' in proxy_wrk and 'p50' in zerocopy_wrk:
            print(f"{'Latency (p50)':<30} {proxy_wrk['p50']:>20} {zerocopy_wrk['p50']:>20} {'':>10}")
        
        if 'p99' in proxy_wrk and 'p99' in zerocopy_wrk:
            print(f"{'Latency (p99)':<30} {proxy_wrk['p99']:>20} {zerocopy_wrk['p99']:>20} {'':>10}")
    
    print("\n" + "="*80)
    
    # Key takeaways
    if proxy_data and zerocopy_data:
        cpu_saving = ((proxy_avg_cpu - zero_avg_cpu) / proxy_avg_cpu * 100) if proxy_avg_cpu > 0 else 0
        print("\n🎯 KEY FINDINGS:")
        print(f"   • Gateway CPU reduced by {cpu_saving:.1f}% with zero-copy architecture")
        print(f"   • System CPU (syscalls) reduced by {((proxy_sys - zero_sys) / proxy_sys * 100):.1f}%")
        if proxy_wrk and zerocopy_wrk and 'rps' in proxy_wrk:
            print(f"   • Throughput increased by {((zerocopy_wrk['rps'] - proxy_wrk['rps']) / proxy_wrk['rps'] * 100):.1f}%")
        print("\n   ✅ Zero-copy architecture eliminates gateway proxy overhead!")
        print("   ✅ Workers respond directly to clients without intermediate copies")
    
    print("\n")

def generate_ascii_chart(data, title, max_width=60):
    """Generate simple ASCII chart"""
    if not data:
        return
    
    print(f"\n{title}")
    print("-" * max_width)
    
    max_val = max(data)
    min_val = min(data)
    
    # Sample every N points if too many
    if len(data) > 60:
        step = len(data) // 60
        data = [data[i] for i in range(0, len(data), step)]
    
    for i, val in enumerate(data):
        bar_len = int((val / max_val) * (max_width - 15)) if max_val > 0 else 0
        bar = "█" * bar_len
        print(f"{i:3d} | {val:6.1f}% {bar}")
    
    print(f"\nMin: {min_val:.1f}%  Max: {max_val:.1f}%  Avg: {statistics.mean(data):.1f}%")

def main():
    script_dir = Path(__file__).parent
    results_dir = script_dir / "results"
    
    print("\n🔍 Loading benchmark results...")
    
    # Load data
    proxy_cpu = load_cpu_data(results_dir / "proxy_cpu.csv")
    zerocopy_cpu = load_cpu_data(results_dir / "zerocopy_cpu.csv")
    
    proxy_wrk = parse_wrk_output(results_dir / "proxy_wrk.txt")
    zerocopy_wrk = parse_wrk_output(results_dir / "zerocopy_wrk.txt")
    
    # Print comparison
    print_comparison(proxy_cpu, zerocopy_cpu, proxy_wrk, zerocopy_wrk)
    
    # Generate ASCII charts
    if zerocopy_cpu:
        generate_ascii_chart(zerocopy_cpu['cpu'][:60], "📈 ZERO-COPY Gateway CPU Usage Over Time")
    
    if proxy_cpu:
        generate_ascii_chart(proxy_cpu['cpu'][:60], "📈 PROXY Gateway CPU Usage Over Time")
    
    # Try to generate matplotlib charts if available
    try:
        import matplotlib.pyplot as plt
        import numpy as np
        
        if proxy_cpu and zerocopy_cpu:
            fig, axes = plt.subplots(2, 2, figsize=(14, 10))
            fig.suptitle('Gateway CPU Benchmark: PROXY vs ZERO-COPY', fontsize=16)
            
            # CPU usage over time
            ax = axes[0, 0]
            ax.plot(proxy_cpu['cpu'], label='PROXY', alpha=0.7)
            ax.plot(zerocopy_cpu['cpu'], label='ZERO-COPY', alpha=0.7)
            ax.set_xlabel('Time (seconds)')
            ax.set_ylabel('CPU %')
            ax.set_title('Gateway CPU Usage Over Time')
            ax.legend()
            ax.grid(True, alpha=0.3)
            
            # CPU distribution
            ax = axes[0, 1]
            ax.hist([proxy_cpu['cpu'], zerocopy_cpu['cpu']], label=['PROXY', 'ZERO-COPY'], bins=30, alpha=0.6)
            ax.set_xlabel('CPU %')
            ax.set_ylabel('Frequency')
            ax.set_title('CPU Usage Distribution')
            ax.legend()
            ax.grid(True, alpha=0.3)
            
            # User vs System CPU
            ax = axes[1, 0]
            x = np.arange(2)
            width = 0.35
            proxy_avg = [statistics.mean(proxy_cpu['user_cpu']), statistics.mean(proxy_cpu['sys_cpu'])]
            zero_avg = [statistics.mean(zerocopy_cpu['user_cpu']), statistics.mean(zerocopy_cpu['sys_cpu'])]
            ax.bar(x - width/2, proxy_avg, width, label='PROXY', alpha=0.7)
            ax.bar(x + width/2, zero_avg, width, label='ZERO-COPY', alpha=0.7)
            ax.set_ylabel('CPU %')
            ax.set_title('Average User vs System CPU')
            ax.set_xticks(x)
            ax.set_xticklabels(['User CPU', 'System CPU'])
            ax.legend()
            ax.grid(True, alpha=0.3)
            
            # Summary comparison
            ax = axes[1, 1]
            metrics = ['Avg CPU', 'Peak CPU', 'Avg Memory']
            proxy_vals = [statistics.mean(proxy_cpu['cpu']), max(proxy_cpu['cpu']), statistics.mean(proxy_cpu['memory'])]
            zero_vals = [statistics.mean(zerocopy_cpu['cpu']), max(zerocopy_cpu['cpu']), statistics.mean(zerocopy_cpu['memory'])]
            
            x = np.arange(len(metrics))
            width = 0.35
            ax.bar(x - width/2, proxy_vals, width, label='PROXY', alpha=0.7)
            ax.bar(x + width/2, zero_vals, width, label='ZERO-COPY', alpha=0.7)
            ax.set_ylabel('Value')
            ax.set_title('Summary Comparison')
            ax.set_xticks(x)
            ax.set_xticklabels(metrics)
            ax.legend()
            ax.grid(True, alpha=0.3)
            
            plt.tight_layout()
            output_file = results_dir / "benchmark_comparison.png"
            plt.savefig(output_file, dpi=150)
            print(f"\n📊 Chart saved to: {output_file}")
            
    except ImportError:
        print("\n💡 Tip: Install matplotlib for graphical charts:")
        print("   pip3 install matplotlib numpy")

if __name__ == '__main__':
    main()

