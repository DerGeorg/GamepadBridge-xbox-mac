#!/usr/bin/env python3
"""
Writes the Sparkle appcast, the feed the app polls for updates.

Kept as a script rather than a here-doc in release.sh because the feed has to
carry every release, not just the newest: Sparkle picks the highest version it
finds, and a feed rewritten from scratch each time would strip the history
that lets someone on an old version see what they are getting.

  make-appcast.py appcast.xml 1.0.1 <url> <length> <edSignature> [notes-file]
"""

import sys
import xml.etree.ElementTree as ET
from email.utils import formatdate
from pathlib import Path

SPARKLE_NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"
ET.register_namespace("sparkle", SPARKLE_NS)

MINIMUM_SYSTEM = "15.0"
RELEASES = "https://gitlab.dergeorg.at/mac/gamepadbridge/-/releases"


def version_key(value):
    return [int(part) if part.isdigit() else 0 for part in value.split(".")]


def load(path):
    if path.exists():
        tree = ET.parse(path)
        return tree, tree.getroot().find("channel")

    rss = ET.Element("rss", {"version": "2.0"})
    channel = ET.SubElement(rss, "channel")

    ET.SubElement(channel, "title").text = "GamepadBridge"
    ET.SubElement(channel, "link").text = (
        "https://gitlab.dergeorg.at/mac/gamepadbridge/-/raw/main/appcast.xml")
    ET.SubElement(channel, "description").text = "GamepadBridge updates"
    ET.SubElement(channel, "language").text = "en"

    return ET.ElementTree(rss), channel


def main():
    if len(sys.argv) < 6:
        sys.exit(__doc__.strip())

    path = Path(sys.argv[1])
    version, url, length, signature = sys.argv[2:6]
    notes = Path(sys.argv[6]).read_text() if len(sys.argv) > 6 else ""

    tree, channel = load(path)

    # Replacing rather than appending keeps a re-run from producing two items
    # for one version, which Sparkle would treat as two separate updates.
    for item in channel.findall("item"):
        found = item.find(f"{{{SPARKLE_NS}}}shortVersionString")

        if found is not None and found.text == version:
            channel.remove(item)

    item = ET.Element("item")
    ET.SubElement(item, "title").text = f"GamepadBridge {version}"
    ET.SubElement(item, "pubDate").text = formatdate(localtime=False)
    ET.SubElement(item, f"{{{SPARKLE_NS}}}version").text = version
    ET.SubElement(item, f"{{{SPARKLE_NS}}}shortVersionString").text = version
    ET.SubElement(item, f"{{{SPARKLE_NS}}}minimumSystemVersion").text = \
        MINIMUM_SYSTEM
    ET.SubElement(item, "link").text = f"{RELEASES}/v{version}"

    if notes.strip():
        ET.SubElement(item, "description").text = notes

    ET.SubElement(item, "enclosure", {
        "url": url,
        "length": str(length),
        "type": "application/octet-stream",
        f"{{{SPARKLE_NS}}}edSignature": signature,
    })

    items = channel.findall("item")

    for existing in items:
        channel.remove(existing)

    items.append(item)
    items.sort(key=lambda node: version_key(
        node.find(f"{{{SPARKLE_NS}}}shortVersionString").text), reverse=True)

    for node in items:
        channel.append(node)

    ET.indent(tree, space="  ")
    tree.write(path, encoding="utf-8", xml_declaration=True)

    print(f"{path}: {len(items)} release(s), newest "
          f"{items[0].find(f'{{{SPARKLE_NS}}}shortVersionString').text}")


if __name__ == "__main__":
    main()
