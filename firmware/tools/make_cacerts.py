"""Writes firmware/dewy/cacerts.h: the root certificates Dewy trusts for HTTPS.

Usage: pip install certifi, then: python3 firmware/tools/make_cacerts.py
It copies the chosen roots from the Mozilla list that certifi ships.
"""
import pathlib
import re

import certifi

ROOTS = [
    "ISRG Root X1", "ISRG Root X2",                              # Let's Encrypt (Cloudflare, Open-Meteo)
    "GTS Root R1", "GTS Root R3", "GTS Root R4",                 # Google Trust Services (Cloudflare)
    "SSL.com Root Certification Authority RSA", "SSL.com Root Certification Authority ECC",
    "SSL.com TLS RSA Root CA 2022", "SSL.com TLS ECC Root CA 2022",  # SSL.com (Cloudflare)
    "USERTrust RSA Certification Authority", "USERTrust ECC Certification Authority",  # Sectigo (github.com)
    "DigiCert Global Root G2", "DigiCert Global Root G3",        # DigiCert (GitHub downloads)
]

pem = pathlib.Path(certifi.where()).read_text()
found = dict(re.findall(r'# Label: "([^"]+)"\n(?:#[^\n]*\n)*(-----BEGIN CERTIFICATE-----\n.*?-----END CERTIFICATE-----)', pem, re.S))
missing = [r for r in ROOTS if r not in found]
if missing:
    raise SystemExit("not in this certifi version: " + ", ".join(missing))

lines = [
    "// Root certificates Dewy trusts for HTTPS: its Cloudflare server, GitHub (firmware downloads)",
    "// and the weather service. Made by firmware/tools/make_cacerts.py from the Mozilla list (certifi " + certifi.__version__ + ").",
    "#pragma once",
    "static const char CA_BUNDLE[] =",
]
for name in ROOTS:
    lines.append("  // " + name)
    lines += ['  "' + l + '\\n"' for l in found[name].split("\n")]
lines.append("  ;")
out = pathlib.Path(__file__).resolve().parent.parent / "dewy" / "cacerts.h"
out.write_text("\n".join(lines) + "\n")
print(f"wrote {out} with {len(ROOTS)} roots")
