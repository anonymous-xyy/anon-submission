import os
import requests
from bs4 import BeautifulSoup
from urllib.parse import urljoin, urlparse
import time
import re

# ===== CONFIGURATION =====
START_URLS = [
    "https://webaim.org/articles/",
    "https://webaim.org/techniques/"
]
OUTPUT_DIR = "webaim_texts"
DELAY = 1
MAX_PAGES = 150

os.makedirs(OUTPUT_DIR, exist_ok=True)

visited = set()
to_visit = set(START_URLS)
page_count = 0

def is_webaim_page(url):
    parsed = urlparse(url)
    # Only pages within webaim.org, skip non-html
    if parsed.netloc not in ('webaim.org', 'www.webaim.org'):
        return False
    if parsed.path.endswith(('.pdf', '.png', '.jpg', '.jpeg', '.gif', '.zip')):
        return False
    # Skip anchor-only links
    if parsed.path == "" and parsed.fragment:
        return False
    return True

def clean_text(soup):
    for element in soup(["script", "style", "nav", "footer", "header", "aside"]):
        element.decompose()
    for element in soup.find_all(class_=re.compile(r"(nav|menu|sidebar|breadcrumb|toc|comment|meta)")):
        element.decompose()
    text = soup.get_text(separator="\n")
    lines = (line.strip() for line in text.splitlines())
    chunks = (phrase.strip() for line in lines for phrase in line.split("  "))
    text = "\n".join(chunk for chunk in chunks if chunk)
    return text

def save_page(url, content, soup):
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
        
        if len(text_content.strip()) > 300:
            save_page(url, text_content, soup)
            page_count += 1
        
        visited.add(url)
        
        for link in soup.find_all('a', href=True):
            absolute_url = urljoin(url, link['href'])
            if is_webaim_page(absolute_url) and absolute_url not in visited:
                to_visit.add(absolute_url)
        
        time.sleep(DELAY)
        
    except Exception as e:
        print(f"Error processing {url}: {e}")
        visited.add(url)

print(f"\nDone. Saved {page_count} pages.")
