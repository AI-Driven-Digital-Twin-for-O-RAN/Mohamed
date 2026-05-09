#!/usr/bin/env python3
"""
ML-Based Handover Analysis Tool
Analyzes output from ml_based_handover_oran.cc simulation

Usage:
    python3 analyze_results.py
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from datetime import datetime
import os

# Set style
sns.set_style("whitegrid")
plt.rcParams['figure.figsize'] = (12, 8)

def load_data():
    """Load all CSV files from simulation"""
    data = {}
    
    files = {
        'handovers': 'ml_handover_events.csv',
        'metrics': 'e2sm_kpm_metrics.csv',
        'e2_reports': 'e2_reports.csv',
        'predictions': 'ml_predictions.csv'
    }
    
    for key, filename in files.items():
        if os.path.exists(filename):
            data[key] = pd.read_csv(filename)
            print(f"✓ Loaded {filename}: {len(data[key])} records")
        else:
            print(f"✗ File not found: {filename}")
    
    return data

def analyze_handovers(df_ho):
    """Analyze handover statistics"""
    print("\n" + "="*70)
    print("HANDOVER ANALYSIS")
    print("="*70)
    
    # Total handovers
    total_hos = len(df_ho[df_ho['Event'] == 'START'])
    completed_hos = len(df_ho[df_ho['Event'] == 'COMPLETE'])
    
    print(f"\nTotal Handovers: {total_hos}")
    print(f"Completed Handovers: {completed_hos}")
    print(f"Success Rate: {(completed_hos/total_hos)*100:.2f}%")
    
    # Per UE statistics
    print("\n--- Handovers per UE ---")
    ue_hos = df_ho[df_ho['Event'] == 'START'].groupby('IMSI').size()
    print(ue_hos.describe())
    
    # Handover flows
    print("\n--- Most Common Handover Flows ---")
    ho_flows = df_ho[df_ho['Event'] == 'START'].groupby(['Source_LTE', 'Target_LTE']).size()
    print(ho_flows.sort_values(ascending=False).head(10))
    
    # Timing analysis
    print("\n--- Handover Timing ---")
    ho_start = df_ho[df_ho['Event'] == 'START'].set_index('IMSI')['Time']
    ho_complete = df_ho[df_ho['Event'] == 'COMPLETE'].set_index('IMSI')['Time']
    
    # Calculate durations (matching IMSIs)
    durations = []
    for imsi in ho_start.index:
        if imsi in ho_complete.index:
            start_times = ho_start[ho_start.index == imsi].values
            complete_times = ho_complete[ho_complete.index == imsi].values
            
            for i in range(min(len(start_times), len(complete_times))):
                duration = (complete_times[i] - start_times[i]) * 1000  # Convert to ms
                if duration > 0:
                    durations.append(duration)
    
    if durations:
        print(f"Average HO Duration: {np.mean(durations):.2f} ms")
        print(f"Min HO Duration: {np.min(durations):.2f} ms")
        print(f"Max HO Duration: {np.max(durations):.2f} ms")
        print(f"Std HO Duration: {np.std(durations):.2f} ms")
    
    return ue_hos, durations

def analyze_metrics(df_metrics):
    """Analyze E2SM-KPM metrics"""
    print("\n" + "="*70)
    print("E2SM-KPM METRICS ANALYSIS")
    print("="*70)
    
    # RSRP statistics
    print("\n--- RSRP Statistics (dBm) ---")
    print(df_metrics['RSRP'].describe())
    
    # SINR statistics
    print("\n--- SINR Statistics (dB) ---")
    print(df_metrics['SINR'].describe())
    
    # RSRQ statistics
    print("\n--- RSRQ Statistics (dB) ---")
    print(df_metrics['RSRQ'].describe())
    
    # CQI statistics
    print("\n--- CQI Statistics [0-15] ---")
    print(df_metrics['CQI'].describe())
    
    # Throughput statistics
    print("\n--- Throughput Statistics (Mbps) ---")
    print("Downlink:")
    print(df_metrics['DL_Throughput'].describe())
    print("\nUplink:")
    print(df_metrics['UL_Throughput'].describe())
    
    # Quality indicators during handovers
    ho_events = df_metrics[df_metrics['Event'].str.contains('SECONDARY|PREDICTION', na=False)]
    
    if len(ho_events) > 0:
        print("\n--- Metrics During Handover Events ---")
        print(f"Average RSRP: {ho_events['RSRP'].mean():.2f} dBm")
        print(f"Average SINR: {ho_events['SINR'].mean():.2f} dB")
        print(f"Average CQI: {ho_events['CQI'].mean():.2f}")

def analyze_ml_predictions(df_pred, df_ho):
    """Analyze ML prediction performance"""
    print("\n" + "="*70)
    print("ML PREDICTION ANALYSIS")
    print("="*70)
    
    # Total predictions
    total_predictions = len(df_pred)
    print(f"\nTotal Predictions Made: {total_predictions}")
    
    if total_predictions == 0:
        print("No predictions recorded.")
        return None, None
    
    # Prediction accuracy (simplified analysis)
    # For each prediction, check if handover occurred within prediction horizon (9s)
    
    predictions_by_ue = df_pred.groupby('IMSI')
    actual_hos = df_ho[df_ho['Event'] == 'START'].groupby('IMSI')
    
    true_positives = 0
    false_positives = 0
    prediction_horizon = 9.0  # seconds
    
    for imsi, pred_group in predictions_by_ue:
        if imsi in actual_hos.groups:
            ho_times = actual_hos.get_group(imsi)['Time'].values
            
            for _, pred_row in pred_group.iterrows():
                pred_time = pred_row['Time']
                target_cell = pred_row['Predicted_Target']
                
                # Check if handover to predicted cell occurred within horizon
                matching_hos = [t for t in ho_times 
                              if pred_time <= t <= pred_time + prediction_horizon]
                
                if len(matching_hos) > 0:
                    # Check if target matches
                    ho_at_time = actual_hos.get_group(imsi)
                    ho_at_time = ho_at_time[ho_at_time['Time'].isin(matching_hos)]
                    
                    if target_cell in ho_at_time['Target_LTE'].values:
                        true_positives += 1
                    else:
                        false_positives += 1
                else:
                    false_positives += 1
        else:
            false_positives += len(pred_group)
    
    # Calculate metrics
    if total_predictions > 0:
        precision = true_positives / (true_positives + false_positives) if (true_positives + false_positives) > 0 else 0
        
        total_actual_hos = len(df_ho[df_ho['Event'] == 'START'])
        recall = true_positives / total_actual_hos if total_actual_hos > 0 else 0
        
        f1_score = 2 * (precision * recall) / (precision + recall) if (precision + recall) > 0 else 0
        
        print(f"\nTrue Positives: {true_positives}")
        print(f"False Positives: {false_positives}")
        print(f"\nPrecision: {precision*100:.2f}%")
        print(f"Recall: {recall*100:.2f}%")
        print(f"F1-Score: {f1_score:.3f}")
        
        # Compare with paper targets
        print(f"\n--- Comparison with Paper Targets ---")
        print(f"Target Precision: 75% | Achieved: {precision*100:.2f}%")
        print(f"Target Recall: 88% | Achieved: {recall*100:.2f}%")
        
        return precision, recall
    
    return None, None

def plot_rsrp_over_time(df_metrics, output_dir='plots'):
    """Plot RSRP evolution over time for each UE"""
    os.makedirs(output_dir, exist_ok=True)
    
    plt.figure(figsize=(14, 8))
    
    for imsi in df_metrics['IMSI'].unique():
        ue_data = df_metrics[df_metrics['IMSI'] == imsi]
        plt.plot(ue_data['Time'], ue_data['RSRP'], 
                label=f'UE {int(imsi)}', alpha=0.7, linewidth=1.5)
    
    plt.xlabel('Time (s)', fontsize=12)
    plt.ylabel('RSRP (dBm)', fontsize=12)
    plt.title('RSRP Evolution Over Time', fontsize=14, fontweight='bold')
    plt.legend(bbox_to_anchor=(1.05, 1), loc='upper left', ncol=2)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    filename = os.path.join(output_dir, 'rsrp_over_time.png')
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    print(f"✓ Saved plot: {filename}")
    plt.close()

def plot_handover_distribution(ue_hos, output_dir='plots'):
    """Plot handover distribution across UEs"""
    os.makedirs(output_dir, exist_ok=True)
    
    plt.figure(figsize=(10, 6))
    
    ue_hos.plot(kind='bar', color='steelblue', edgecolor='black')
    
    plt.xlabel('UE IMSI', fontsize=12)
    plt.ylabel('Number of Handovers', fontsize=12)
    plt.title('Handover Distribution per UE', fontsize=14, fontweight='bold')
    plt.xticks(rotation=45)
    plt.grid(True, axis='y', alpha=0.3)
    plt.tight_layout()
    
    filename = os.path.join(output_dir, 'handover_distribution.png')
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    print(f"✓ Saved plot: {filename}")
    plt.close()

def plot_sinr_cqi_relationship(df_metrics, output_dir='plots'):
    """Plot relationship between SINR and CQI"""
    os.makedirs(output_dir, exist_ok=True)
    
    plt.figure(figsize=(10, 6))
    
    # Sample data to avoid overcrowding
    sample_size = min(1000, len(df_metrics))
    df_sample = df_metrics.sample(n=sample_size, random_state=42)
    
    plt.scatter(df_sample['SINR'], df_sample['CQI'], 
               alpha=0.5, s=30, c=df_sample['RSRP'], 
               cmap='viridis', edgecolors='black', linewidth=0.5)
    
    plt.colorbar(label='RSRP (dBm)')
    plt.xlabel('SINR (dB)', fontsize=12)
    plt.ylabel('CQI [0-15]', fontsize=12)
    plt.title('SINR vs CQI Relationship', fontsize=14, fontweight='bold')
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    filename = os.path.join(output_dir, 'sinr_cqi_relationship.png')
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    print(f"✓ Saved plot: {filename}")
    plt.close()

def plot_handover_timing(durations, output_dir='plots'):
    """Plot handover duration distribution"""
    if not durations:
        print("No handover duration data available.")
        return
    
    os.makedirs(output_dir, exist_ok=True)
    
    plt.figure(figsize=(10, 6))
    
    plt.hist(durations, bins=30, color='coral', edgecolor='black', alpha=0.7)
    
    plt.axvline(np.mean(durations), color='red', linestyle='--', 
               linewidth=2, label=f'Mean: {np.mean(durations):.2f} ms')
    
    plt.xlabel('Handover Duration (ms)', fontsize=12)
    plt.ylabel('Frequency', fontsize=12)
    plt.title('Handover Duration Distribution', fontsize=14, fontweight='bold')
    plt.legend()
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    filename = os.path.join(output_dir, 'handover_duration.png')
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    print(f"✓ Saved plot: {filename}")
    plt.close()

def plot_throughput_over_time(df_metrics, output_dir='plots'):
    """Plot throughput evolution"""
    os.makedirs(output_dir, exist_ok=True)
    
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 10))
    
    # Downlink
    for imsi in df_metrics['IMSI'].unique()[:5]:  # Plot first 5 UEs
        ue_data = df_metrics[df_metrics['IMSI'] == imsi]
        ax1.plot(ue_data['Time'], ue_data['DL_Throughput'], 
                label=f'UE {int(imsi)}', alpha=0.7, linewidth=1.5)
    
    ax1.set_xlabel('Time (s)', fontsize=12)
    ax1.set_ylabel('DL Throughput (Mbps)', fontsize=12)
    ax1.set_title('Downlink Throughput Over Time', fontsize=13, fontweight='bold')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    
    # Uplink
    for imsi in df_metrics['IMSI'].unique()[:5]:
        ue_data = df_metrics[df_metrics['IMSI'] == imsi]
        ax2.plot(ue_data['Time'], ue_data['UL_Throughput'], 
                label=f'UE {int(imsi)}', alpha=0.7, linewidth=1.5)
    
    ax2.set_xlabel('Time (s)', fontsize=12)
    ax2.set_ylabel('UL Throughput (Mbps)', fontsize=12)
    ax2.set_title('Uplink Throughput Over Time', fontsize=13, fontweight='bold')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    filename = os.path.join(output_dir, 'throughput_over_time.png')
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    print(f"✓ Saved plot: {filename}")
    plt.close()

def generate_report(data, output_file='simulation_report.txt'):
    """Generate comprehensive text report"""
    
    with open(output_file, 'w') as f:
        f.write("="*70 + "\n")
        f.write("ML-BASED HANDOVER PREDICTION SIMULATION REPORT\n")
        f.write("="*70 + "\n")
        f.write(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write("="*70 + "\n\n")
        
        # Handover statistics
        if 'handovers' in data:
            df_ho = data['handovers']
            total_hos = len(df_ho[df_ho['Event'] == 'START'])
            completed_hos = len(df_ho[df_ho['Event'] == 'COMPLETE'])
            
            f.write("HANDOVER STATISTICS\n")
            f.write("-" * 70 + "\n")
            f.write(f"Total Handovers: {total_hos}\n")
            f.write(f"Completed Handovers: {completed_hos}\n")
            f.write(f"Success Rate: {(completed_hos/total_hos)*100:.2f}%\n\n")
        
        # Metrics statistics
        if 'metrics' in data:
            df_metrics = data['metrics']
            
            f.write("PERFORMANCE METRICS\n")
            f.write("-" * 70 + "\n")
            f.write(f"Average RSRP: {df_metrics['RSRP'].mean():.2f} dBm\n")
            f.write(f"Average SINR: {df_metrics['SINR'].mean():.2f} dB\n")
            f.write(f"Average RSRQ: {df_metrics['RSRQ'].mean():.2f} dB\n")
            f.write(f"Average CQI: {df_metrics['CQI'].mean():.2f}\n")
            f.write(f"Average DL Throughput: {df_metrics['DL_Throughput'].mean():.2f} Mbps\n")
            f.write(f"Average UL Throughput: {df_metrics['UL_Throughput'].mean():.2f} Mbps\n\n")
        
        # ML predictions
        if 'predictions' in data and len(data['predictions']) > 0:
            f.write("ML PREDICTION PERFORMANCE\n")
            f.write("-" * 70 + "\n")
            f.write(f"Total Predictions: {len(data['predictions'])}\n")
            f.write("(See detailed analysis above for precision/recall)\n\n")
        
        f.write("="*70 + "\n")
        f.write("END OF REPORT\n")
        f.write("="*70 + "\n")
    
    print(f"✓ Report saved: {output_file}")

def main():
    """Main analysis function"""
    print("\n" + "="*70)
    print("ML-BASED HANDOVER PREDICTION - RESULTS ANALYSIS")
    print("="*70 + "\n")
    
    # Load data
    data = load_data()
    
    if not data:
        print("\n✗ No data files found. Please run the simulation first.")
        return
    
    # Analyze handovers
    ue_hos = None
    durations = None
    if 'handovers' in data:
        ue_hos, durations = analyze_handovers(data['handovers'])
    
    # Analyze metrics
    if 'metrics' in data:
        analyze_metrics(data['metrics'])
    
    # Analyze ML predictions
    if 'predictions' in data and 'handovers' in data:
        analyze_ml_predictions(data['predictions'], data['handovers'])
    
    # Generate plots
    print("\n" + "="*70)
    print("GENERATING PLOTS")
    print("="*70 + "\n")
    
    if 'metrics' in data:
        plot_rsrp_over_time(data['metrics'])
        plot_sinr_cqi_relationship(data['metrics'])
        plot_throughput_over_time(data['metrics'])
    
    if ue_hos is not None:
        plot_handover_distribution(ue_hos)
    
    if durations:
        plot_handover_timing(durations)
    
    # Generate report
    print("\n" + "="*70)
    print("GENERATING REPORT")
    print("="*70 + "\n")
    
    generate_report(data)
    
    print("\n" + "="*70)
    print("ANALYSIS COMPLETE")
    print("="*70)
    print("\nCheck the 'plots/' directory for visualizations.")
    print("Check 'simulation_report.txt' for detailed statistics.\n")

if __name__ == "__main__":
    main()
