"""HTML clean-up applied by mkdocs-llmstxt before converting pages to Markdown."""

from bs4 import BeautifulSoup


def preprocess(soup: BeautifulSoup, output: str) -> None:
    # Material card grids use <hr> as a visual divider; in Markdown it becomes
    # a stray thematic break inside every card.
    for hr in soup.select(".grid.cards hr"):
        hr.decompose()

    # Mermaid diagrams render as <pre class="mermaid">; add a language class so
    # the Markdown fence is tagged ```mermaid and stays recognizable as a diagram.
    for pre in soup.select("pre.mermaid"):
        pre["class"] = [*pre.get("class", []), "language-mermaid"]
