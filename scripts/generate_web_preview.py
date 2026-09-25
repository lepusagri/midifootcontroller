"""Build an offline preview from the web UI shipped with the firmware."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_HTML = ROOT / "src" / "web.html"
SOURCE_JS = ROOT / "src" / "web.js"
MOCK_JS = ROOT / "docs" / "preview" / "mock-api.js"
OUTPUT = ROOT / "docs" / "preview" / "index.html"


def main():
    html = SOURCE_HTML.read_text(encoding="utf-8")
    app_script = SOURCE_JS.read_text(encoding="utf-8")
    mock_script = MOCK_JS.read_text(encoding="utf-8")
    html = html.replace(
        "  <script>\n/*LEPUS_SCRIPT*/",
        "  <script>\n" + mock_script + "\n  </script>\n  <script>\n" + app_script,
    )
    OUTPUT.write_text(html, encoding="utf-8")
    print(OUTPUT)


if __name__ == "__main__":
    main()
