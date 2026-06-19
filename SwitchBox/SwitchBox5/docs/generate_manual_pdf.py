from __future__ import annotations

import html
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
MANUAL_MD = ROOT / "CAB_SWITCHBOX_MANUAL.md"
OUTPUT_HTML = ROOT / "CAB_SWITCHBOX_MANUAL_PRINT.html"


def slugify(text: str) -> str:
    value = re.sub(r"[^a-zA-Z0-9]+", "-", text.strip().lower()).strip("-")
    return value or "section"


def parse_inline(text: str) -> str:
    escaped = html.escape(text)
    escaped = re.sub(r"`([^`]+)`", r"<code>\1</code>", escaped)
    escaped = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", escaped)
    return escaped


def table_to_html(lines: list[str]) -> str:
    rows: list[list[str]] = []
    for line in lines:
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        rows.append(cells)

    out = ["<div class=\"table-wrap\"><table>"]
    if rows:
        out.append("<thead><tr>")
        for cell in rows[0]:
            out.append(f"<th>{parse_inline(cell)}</th>")
        out.append("</tr></thead>")

    out.append("<tbody>")
    for row in rows[2:]:
        out.append("<tr>")
        for cell in row:
            out.append(f"<td>{parse_inline(cell)}</td>")
        out.append("</tr>")
    out.append("</tbody></table></div>")
    return "".join(out)


def markdown_to_html(markdown: str) -> str:
    lines = markdown.splitlines()
    out: list[str] = []
    i = 0
    in_code = False
    in_ul = False
    in_ol = False

    while i < len(lines):
        line = lines[i]

        if line.startswith("```"):
            if in_ul:
                out.append("</ul>")
                in_ul = False
            if in_ol:
                out.append("</ol>")
                in_ol = False
            if not in_code:
                out.append("<pre><code>")
                in_code = True
            else:
                out.append("</code></pre>")
                in_code = False
            i += 1
            continue

        if in_code:
            out.append(html.escape(line) + "\n")
            i += 1
            continue

        if line.strip().startswith("|") and i + 1 < len(lines) and set(lines[i + 1].replace("|", "").strip()) <= {"-", ":", " "}:
            if in_ul:
                out.append("</ul>")
                in_ul = False
            if in_ol:
                out.append("</ol>")
                in_ol = False
            table_lines = [line, lines[i + 1]]
            i += 2
            while i < len(lines) and lines[i].strip().startswith("|"):
                table_lines.append(lines[i])
                i += 1
            out.append(table_to_html(table_lines))
            continue

        stripped = line.strip()
        if not stripped:
            if in_ul:
                out.append("</ul>")
                in_ul = False
            if in_ol:
                out.append("</ol>")
                in_ol = False
            i += 1
            continue

        heading = re.match(r"^(#{1,6})\s+(.+)$", stripped)
        if heading:
            if in_ul:
                out.append("</ul>")
                in_ul = False
            if in_ol:
                out.append("</ol>")
                in_ol = False
            level = len(heading.group(1))
            text = heading.group(2)
            out.append(f"<h{level} id=\"{slugify(text)}\">{parse_inline(text)}</h{level}>")
            i += 1
            continue

        ordered = re.match(r"^\d+\.\s+(.+)$", stripped)
        if ordered:
            if in_ul:
                out.append("</ul>")
                in_ul = False
            if not in_ol:
                out.append("<ol>")
                in_ol = True
            out.append(f"<li>{parse_inline(ordered.group(1))}</li>")
            i += 1
            continue

        if stripped.startswith("- "):
            if in_ol:
                out.append("</ol>")
                in_ol = False
            if not in_ul:
                out.append("<ul>")
                in_ul = True
            out.append(f"<li>{parse_inline(stripped[2:])}</li>")
            i += 1
            continue

        if in_ul:
            out.append("</ul>")
            in_ul = False
        if in_ol:
            out.append("</ol>")
            in_ol = False
        out.append(f"<p>{parse_inline(stripped)}</p>")
        i += 1

    if in_ul:
        out.append("</ul>")
    if in_ol:
        out.append("</ol>")
    return "\n".join(out)


def build_html() -> str:
    body = markdown_to_html(MANUAL_MD.read_text(encoding="utf-8"))
    terminal_diagram = """
      <section class="screenshots">
        <h2>Board Power And Switch Feed</h2>
        <figure>
          <img src="manual-assets/BoardPNG_R01.png" alt="SwitchBox5 Rev 01 board terminal layout">
          <figcaption>SwitchBox5-Terminals-PCB-01 (Rev 01). Connect board power to 12Vin and GND. Use the fused 12V terminals through switches to power the IN0-IN15 inputs.</figcaption>
        </figure>
      </section>
    """
    screenshots = """
      <section class="screenshots">
        <h2>Setup Page Screenshots</h2>
        <figure>
          <img src="manual-assets/cab-switchbox-desktop.png" alt="Cab Switchbox setup page desktop screenshot">
          <figcaption>Desktop setup page with live status, decoded packet, network settings, and input configuration.</figcaption>
        </figure>
        <figure>
          <img src="manual-assets/cab-switchbox-mobile.png" alt="Cab Switchbox setup page mobile screenshot">
          <figcaption>Mobile layout used when configuring the switchbox from a phone or tablet in the cab.</figcaption>
        </figure>
      </section>
    """
    body = body.replace("<h2 id=\"power-and-switch-feed-terminals\">", terminal_diagram + "<h2 id=\"power-and-switch-feed-terminals\">", 1)
    body = body.replace("<h2 id=\"setup-page-sections\">", screenshots + "<h2 id=\"setup-page-sections\">", 1)

    return f"""<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>Cab Switchbox Manual</title>
  <style>
    @page {{ size: Letter; margin: 0.55in; }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      color: #1f2933;
      font-family: Arial, Helvetica, sans-serif;
      font-size: 10.5pt;
      line-height: 1.45;
      background: white;
    }}
    h1, h2, h3, h4 {{ color: #16313d; line-height: 1.15; page-break-after: avoid; }}
    h1 {{
      margin: 0 0 8px;
      font-size: 30pt;
      letter-spacing: 0;
    }}
    h2 {{
      margin: 24px 0 8px;
      padding-top: 6px;
      border-top: 2px solid #d7e3e8;
      font-size: 17pt;
    }}
    h3 {{ margin: 16px 0 6px; font-size: 13pt; }}
    p {{ margin: 7px 0; }}
    ul, ol {{ margin: 7px 0 10px 22px; padding: 0; }}
    li {{ margin: 3px 0; }}
    code {{
      padding: 1px 4px;
      border-radius: 3px;
      background: #edf3f5;
      color: #14323f;
      font-family: Consolas, "Courier New", monospace;
      font-size: 9.3pt;
    }}
    pre {{
      margin: 10px 0;
      padding: 10px 12px;
      border-left: 4px solid #27718e;
      background: #f1f6f7;
      page-break-inside: avoid;
      white-space: pre-wrap;
    }}
    pre code {{ padding: 0; background: transparent; }}
    .table-wrap {{ margin: 10px 0 14px; page-break-inside: avoid; }}
    table {{ width: 100%; border-collapse: collapse; font-size: 9.4pt; }}
    th, td {{ border: 1px solid #cbd7dc; padding: 6px 7px; vertical-align: top; }}
    th {{ background: #e6f0f3; color: #16313d; text-align: left; }}
    tr:nth-child(even) td {{ background: #f8fbfc; }}
    .screenshots {{ margin: 18px 0; }}
    figure {{
      margin: 12px 0 18px;
      padding: 10px;
      border: 1px solid #cbd7dc;
      border-radius: 6px;
      background: #f8fbfc;
      page-break-inside: avoid;
    }}
    figure img {{
      display: block;
      width: 100%;
      max-height: 7.2in;
      object-fit: contain;
      border: 1px solid #d8e1e5;
      background: white;
    }}
    figcaption {{ margin-top: 7px; color: #52616b; font-size: 9.1pt; }}
    h1 + p {{
      margin: 0 0 18px;
      color: #52616b;
      font-size: 11pt;
    }}
    h1::after {{
      content: "ESP32 network switch input setup, wiring, and troubleshooting";
      display: block;
      margin-top: 10px;
      padding: 10px 12px;
      border-left: 5px solid #27718e;
      background: #edf5f7;
      color: #29434e;
      font-size: 12pt;
      font-weight: normal;
    }}
  </style>
</head>
<body>
{body}
</body>
</html>
"""


def main() -> None:
    OUTPUT_HTML.write_text(build_html(), encoding="utf-8")
    print(OUTPUT_HTML)


if __name__ == "__main__":
    main()
