import os
import requests
from bs4 import BeautifulSoup
from urllib.parse import urljoin, urlparse
import time
import re

# ===== CONFIGURATION =====
START_URL = "https://www.w3.org/WAI/"
OUTPUT_DIR = "wai_texts"
DELAY = 1  # seconds between requests to be respectful
MAX_PAGES = 100  # set a limit for testing; remove or increase for full run

# Create output directory
os.makedirs(OUTPUT_DIR, exist_ok=True)

visited = set()
to_visit = {START_URL}
page_count = 0

def is_wai_page(url):
    """Return True if URL is within the WAI section and is not a fragment or file."""
    parsed = urlparse(url)
    # Only keep pages within /WAI/ path, skip non-html files
    if not parsed.path.startswith("/WAI/"):
        return False
    # Skip common non-content file types
    if parsed.path.endswith(('.pdf', '.png', '.jpg', '.jpeg', '.gif', '.svg', '.zip', '.mp3', '.mp4')):
        return False
    # Skip fragment-only links (they point to same page)
    if parsed.path == "" and parsed.fragment:
        return False
    return True

def clean_text(soup):
    """Extract and clean text from a BeautifulSoup object."""
    # Remove script, style, nav, footer elements
    for element in soup(["script", "style", "nav", "footer", "header", "aside"]):
        element.decompose()
    
    # Optionally, remove elements by class (adjust as needed)
    for element in soup.find_all(class_=re.compile(r"(nav|menu|sidebar|breadcrumb|toc)")):
        element.decompose()
    
    # Get text and clean up whitespace
    text = soup.get_text(separator="\n")
    lines = (line.strip() for line in text.splitlines())
    chunks = (phrase.strip() for line in lines for phrase in line.split("  "))
    text = "\n".join(chunk for chunk in chunks if chunk)
    return text

def save_page(url, content):
    """Save the cleaned content to a file."""
    # Create a filename from the URL
    path = urlparse(url).path.strip("/").replace("/", "_")
    if not path:
        path = "index"
    filename = os.path.join(OUTPUT_DIR, f"{path}.txt")
    with open(filename, "w", encoding="utf-8") as f:
        f.write(f"URL: {url}\n")
        f.write(f"Title: {soup.title.string.strip() if soup.title else ''}\n")
        f.write("="*80 + "\n\n")
        f.write(content)
    print(f"Saved: {filename}")

# === MAIN CRAWLING LOOP ===
while to_visit and page_count < MAX_PAGES:
    url = to_visit.pop()
    if url in visited:
        continue
    
    print(f"Crawling: {url}")
    try:
        response = requests.get(url, timeout=10)
        response.raise_for_status()
        
        soup = BeautifulSoup(response.text, 'html.parser')
        text_content = clean_text(soup)
        
        if len(text_content.strip()) > 200:  # Only save substantial content
            save_page(url, text_content)
            page_count += 1
        
        visited.add(url)
        
        # Extract links
        for link in soup.find_all('a', href=True):
            absolute_url = urljoin(url, link['href'])
            if is_wai_page(absolute_url) and absolute_url not in visited:
                to_visit.add(absolute_url)
        
        time.sleep(DELAY)
        
    except Exception as e:
        print(f"Error processing {url}: {e}")
        visited.add(url)  # mark as visited to avoid retrying

print(f"\nDone. Saved {page_count} pages.")
