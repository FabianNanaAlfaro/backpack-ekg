# Publishing the interactive site

The static site lives in [`site/`](../site/) and is published at [fabiannanaalfaro.github.io/backpack-ekg](https://fabiannanaalfaro.github.io/backpack-ekg/). GitHub Pages is enabled for the repository, and the deployment runs automatically on every push to `main`; a manual trigger remains available for an intentional rebuild.

The repository owner account has already completed the one-time Pages configuration. For a manual rebuild:

1. Open **Actions → publish interactive project site**.
2. Select **Run workflow** on the `main` branch.

Each deployment builds the `site/` folder and deploys the interactive gallery. The workflow does not upload any database, recording, calibration file or other private artifact.

For local preview, serve the repository root with any static file server and open `site/index.html`; the repository keeps a copy of the public media under `site/assets/` so the page can also be previewed directly from that folder.
