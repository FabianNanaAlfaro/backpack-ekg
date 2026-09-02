# Publishing the interactive site

The static site lives in [`site/`](../site/) and is ready for GitHub Pages. It is kept as a manual workflow because enabling a Pages site is an account-level repository setting that the connected GitHub token cannot change.

To publish it from the repository owner account:

1. Open **Settings → Pages** in this repository.
2. Select **GitHub Actions** as the build and deployment source.
3. Run **Actions → publish interactive project site → Run workflow** once.

After Pages is enabled, future manual runs will build the `site/` folder and deploy the interactive gallery. The workflow does not upload any database, recording, calibration file or other private artifact.

For local preview, serve the repository root with any static file server and open `site/index.html`; the repository keeps a copy of the public media under `site/assets/` so the page can also be previewed directly from that folder.

