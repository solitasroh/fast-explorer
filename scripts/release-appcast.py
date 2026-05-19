"""Sign a release ZIP with Ed25519 and append it to appcast/appcast.xml.

Used by .github/workflows/release.yml after CPack produces the release archive.
Can also be run locally for testing.

Usage:
    # one-time keypair generation (writes ed25519_priv.b64 and ed25519_pub.b64)
    python scripts/release-appcast.py keygen

    # sign + append (CI invocation)
    python scripts/release-appcast.py publish \\
        --zip       build/FastExplorer-0.1.0-win64.zip \\
        --version   0.1.0 \\
        --url       https://github.com/solitasroh/fast-explorer/releases/download/v0.1.0/FastExplorer-0.1.0-win64.zip \\
        --appcast   appcast/appcast.xml \\
        --priv-key  "$ED25519_PRIVATE_KEY"
"""

from __future__ import annotations

import argparse
import base64
import sys
from datetime import datetime, timezone
from pathlib import Path
from xml.etree import ElementTree as ET

try:
    import nacl.signing  # type: ignore
except ImportError:
    sys.stderr.write(
        "PyNaCl is required: pip install pynacl\n"
    )
    sys.exit(2)


SPARKLE_NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"
ET.register_namespace("sparkle", SPARKLE_NS)


def cmd_keygen(_: argparse.Namespace) -> int:
    sk = nacl.signing.SigningKey.generate()
    vk = sk.verify_key
    priv_b64 = base64.b64encode(sk.encode()).decode("ascii")
    pub_b64 = base64.b64encode(vk.encode()).decode("ascii")
    Path("ed25519_priv.b64").write_text(priv_b64 + "\n", encoding="ascii")
    Path("ed25519_pub.b64").write_text(pub_b64 + "\n", encoding="ascii")
    print(f"Wrote ed25519_priv.b64 (KEEP SECRET) and ed25519_pub.b64.")
    print(f"Public key (embed in build via -DFE_EDDSA_PUBLIC_KEY=...):\n  {pub_b64}")
    return 0


def sign_file(path: Path, priv_b64: str) -> str:
    sk = nacl.signing.SigningKey(base64.b64decode(priv_b64))
    sig = sk.sign(path.read_bytes()).signature
    return base64.b64encode(sig).decode("ascii")


def append_item(appcast_path: Path, version: str, url: str, length: int,
                signature_b64: str, pubdate: str) -> None:
    tree = ET.parse(appcast_path)
    channel = tree.getroot().find("channel")
    if channel is None:
        raise SystemExit(f"{appcast_path}: no <channel> element")

    if any((it.find("enclosure") is not None and
            it.find("enclosure").get(f"{{{SPARKLE_NS}}}version") == version)
           for it in channel.findall("item")):
        raise SystemExit(f"appcast already contains version {version}; refusing to duplicate")

    item = ET.SubElement(channel, "item")
    ET.SubElement(item, "title").text = f"Version {version}"
    ET.SubElement(item, "pubDate").text = pubdate
    enc = ET.SubElement(item, "enclosure")
    enc.set("url", url)
    enc.set(f"{{{SPARKLE_NS}}}version", version)
    enc.set(f"{{{SPARKLE_NS}}}edSignature", signature_b64)
    enc.set("length", str(length))
    enc.set("type", "application/octet-stream")

    ET.indent(tree, space="  ")
    tree.write(appcast_path, encoding="utf-8", xml_declaration=True)


def cmd_publish(args: argparse.Namespace) -> int:
    zip_path = Path(args.zip)
    if not zip_path.is_file():
        raise SystemExit(f"{zip_path} not found")

    if not args.priv_key:
        raise SystemExit("--priv-key (or ED25519_PRIVATE_KEY env) is required")

    signature = sign_file(zip_path, args.priv_key)
    pubdate = datetime.now(timezone.utc).strftime("%a, %d %b %Y %H:%M:%S +0000")

    append_item(
        appcast_path=Path(args.appcast),
        version=args.version,
        url=args.url,
        length=zip_path.stat().st_size,
        signature_b64=signature,
        pubdate=pubdate,
    )
    print(f"appcast updated with version {args.version}")
    print(f"  signature: {signature}")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="WinSparkle release helper")
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("keygen", help="generate a new Ed25519 keypair").set_defaults(func=cmd_keygen)

    pub = sub.add_parser("publish", help="sign ZIP and append to appcast.xml")
    pub.add_argument("--zip", required=True, help="path to release ZIP")
    pub.add_argument("--version", required=True, help="release version, e.g. 0.1.0")
    pub.add_argument("--url", required=True, help="public download URL for the ZIP")
    pub.add_argument("--appcast", required=True, help="path to appcast.xml")
    pub.add_argument("--priv-key", required=True, help="base64 Ed25519 private key")
    pub.set_defaults(func=cmd_publish)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
