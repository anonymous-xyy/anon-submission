import os
from pathlib import Path
import pandas as pd
from collections import defaultdict
import re
import matplotlib.pyplot as plt
import matplotlib
matplotlib.use('Agg')  # For non-GUI environments

# ===== CONFIGURATION =====
SOURCE_DIRS = {
    'W3C_WAI_Main': 'wai_texts',
    'WebAIM': 'webaim_texts', 
    'W3C_WAI_Technical': 'w3c_accessibility_texts'
}

def get_text_stats(directory_path):
    """Collect statistics for all text files in directory"""
    if not os.path.exists(directory_path):
        return None
    
    stats = []
    for file_path in Path(directory_path).glob('*.txt'):
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
                total_chars = len(content)
                
                # Exclude metadata (URL and Title) from content calculation
                lines = content.split('\n')
                if len(lines) > 3 and lines[0].startswith('URL:'):
                    main_content = '\n'.join(lines[3:]) if len(lines) > 3 else ''
                    content_chars = len(main_content)
                else:
                    content_chars = total_chars
                
                stats.append({
                    'file': file_path.name,
                    'total_chars': total_chars,
                    'content_chars': content_chars
                })
        except Exception as e:
            print(f"Error reading {file_path}: {e}")
            continue
    
    if not stats:
        return None
    
    df = pd.DataFrame(stats)
    return {
        'total_files': len(df),
        'total_chars': df['total_chars'].sum(),
        'total_content_chars': df['content_chars'].sum(),
        'avg_file_size': df['total_chars'].mean(),
        'avg_content_size': df['content_chars'].mean(),
        'min_size': df['total_chars'].min(),
        'max_size': df['total_chars'].max(),
        'files': df
    }

def print_statistics_table():
    """Print formatted statistics table"""
    print("\n" + "="*80)
    print("DATASET STATISTICS BY SOURCE")
    print("="*80)
    
    all_data = {}
    for source_name, dir_path in SOURCE_DIRS.items():
        print(f"\nProcessing: {source_name}...")
        stats = get_text_stats(dir_path)
        if stats:
            all_data[source_name] = stats
    
    # Create summary table
    table_data = []
    for source, stats in all_data.items():
        table_data.append({
            'Source': source,
            'Documents': stats['total_files'],
            'Total chars': f"{stats['total_chars']:,}",
            'Content chars': f"{stats['total_content_chars']:,}",
            'Avg size': f"{stats['avg_file_size']:.0f}",
            'Avg content': f"{stats['avg_content_size']:.0f}",
            'Min': f"{stats['min_size']:,}",
            'Max': f"{stats['max_size']:,}"
        })
    
    df_table = pd.DataFrame(table_data)
    print("\n" + df_table.to_string(index=False))
    
    # Overall totals
    total_files = sum(s['total_files'] for s in all_data.values())
    total_chars = sum(s['total_chars'] for s in all_data.values())
    total_content = sum(s['total_content_chars'] for s in all_data.values())
    
    print("\n" + "-"*80)
    print("TOTALS ACROSS ALL SOURCES:")
    print(f"  Total documents: {total_files}")
    print(f"  Total characters (with metadata): {total_chars:,}")
    print(f"  Total characters (content only): {total_content:,}")
    print("="*80)
    
    return all_data

def analyze_topics_by_filename():
    """Analyze topic distribution based on filename patterns"""
    topic_patterns = {
        'WCAG Guidelines': [r'WCAG', r'wcag', r'quickref', r'guidelines', r'Understanding'],
        'ARIA': [r'ARIA', r'aria', r'apg'],
        'Techniques': [r'Techniques', r'techniques', r'G\d+', r'H\d+', r'F\d+', r'SCR\d+', r'C\d+', r'PDF\d+'],
        'Tutorials': [r'tutorials', r'Tutorials'],
        'Testing_Evaluation': [r'test-evaluate', r'evaluation', r'conformance', r'checklist'],
        'Planning_Business': [r'planning', r'business-case', r'involving-users', r'statements', r'strategicframework'],
        'Screen_Readers': [r'screenreader', r'jaws', r'nvda', r'voiceover', r'narrator'],
        'Cognitive': [r'cognitive', r'Cognitive'],
        'Forms': [r'forms', r'Forms', r'validation', r'formvalidation'],
        'Tables': [r'tables', r'Tables'],
        'Images_AltText': [r'images', r'alttext', r'alt-text'],
        'Keyboard': [r'keyboard', r'Keyboard'],
        'Contrast': [r'contrast', r'Contrast'],
        'PDF': [r'pdf', r'PDF'],
        'HTML_CSS': [r'html', r'HTML', r'css', r'CSS'],
        'JavaScript': [r'javascript', r'script', r'client-side-script']
    }
    
    topic_counts = defaultdict(int)
    
    for source_name, dir_path in SOURCE_DIRS.items():
        if not os.path.exists(dir_path):
            continue
        
        for file_path in Path(dir_path).glob('*.txt'):
            filename = file_path.name
            matched = False
            for topic, patterns in topic_patterns.items():
                for pattern in patterns:
                    if re.search(pattern, filename, re.IGNORECASE):
                        topic_counts[topic] += 1
                        matched = True
                        break
                if matched:
                    break
            if not matched:
                topic_counts['General_Accessibility'] += 1
    
    return topic_counts

def plot_topic_distribution(topic_counts):
    """Generate and save matplotlib histogram of topic distribution"""
    # Sort by count descending
    sorted_items = sorted(topic_counts.items(), key=lambda x: x[1], reverse=True)
    topics = [item[0] for item in sorted_items]
    counts = [item[1] for item in sorted_items]
    
    # Create figure with two subplots
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 8))
    
    # Horizontal bar chart (better for many categories)
    y_pos = range(len(topics))
    ax1.barh(y_pos, counts, color='steelblue', edgecolor='navy', alpha=0.7)
    ax1.set_yticks(y_pos)
    ax1.set_yticklabels(topics)
    ax1.set_xlabel('Number of Documents', fontsize=11)
    ax1.set_title('Topic Distribution (Horizontal)', fontsize=13, fontweight='bold')
    ax1.invert_yaxis()  # Display highest count at top
    
    # Add value labels on bars
    for i, v in enumerate(counts):
        ax1.text(v + 0.5, i, str(v), va='center', fontsize=9)
    
    # Pie chart for proportion visualization (top 8 categories)
    top_n = 8
    top_topics = topics[:top_n]
    top_counts = counts[:top_n]
    other_count = sum(counts[top_n:])
    
    if other_count > 0:
        top_topics.append('Other')
        top_counts.append(other_count)
    
    colors = plt.cm.Set3(range(len(top_topics)))
    wedges, texts, autotexts = ax2.pie(top_counts, labels=top_topics, autopct='%1.1f%%',
                                        colors=colors, startangle=90)
    ax2.set_title(f'Topic Distribution (Top {top_n} Categories + Other)', 
                  fontsize=13, fontweight='bold')
    
    plt.tight_layout()
    plt.savefig('topic_distribution.png', dpi=150, bbox_inches='tight')
    print("\n✓ Saved histogram as 'topic_distribution.png'")
    plt.close()

def print_topic_summary(topic_counts):
    """Print topic distribution summary to console"""
    print("\n" + "="*80)
    print("TOPIC DISTRIBUTION ANALYSIS")
    print("="*80)
    
    sorted_topics = sorted(topic_counts.items(), key=lambda x: x[1], reverse=True)
    
    print("\n{:<30} {:>10} {:>15}".format("Topic Category", "Documents", "Percentage"))
    print("-" * 80)
    
    total = sum(topic_counts.values())
    for topic, count in sorted_topics:
        percentage = (count / total) * 100
        print("{:<30} {:>10} {:>14.1f}%".format(topic, count, percentage))
    
    print("-" * 80)
    print("{:<30} {:>10}".format("TOTAL", total))
    print("="*80)

if __name__ == "__main__":
    # Get statistics
    stats = print_statistics_table()
    
    # Analyze topics
    topic_counts = analyze_topics_by_filename()
    
    # Print topic summary
    print_topic_summary(topic_counts)
    
    # Generate and save matplotlib visualizations
    plot_topic_distribution(topic_counts)
    
    print(f"\nAnalysis complete. Processed {sum(s['total_files'] for s in stats.values())} documents total.")
