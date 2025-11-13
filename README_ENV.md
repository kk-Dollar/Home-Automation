# Environment & Git guidance for this project

This project uses a `.env` file for secrets and environment-specific configuration. The repository includes a `.env.example` file with the variables you should set. Your real `.env` file is ignored by `.gitignore` so secrets won't be committed.

Files added
- `.env.example` - template of environment variables (copy to `.env` and edit)
- `.env` - your local file (do NOT commit)
- `scripts/load_env_and_build.py` - loads `.env` into environment and runs PlatformIO CLI
- `scripts/build.sh` - simple bash wrapper to call the python loader

Quick start

1. Copy the example to create your local env file:

```bash
cp .env.example .env
# Edit .env with your values
```

2. Build the project with the env loaded (recommended):

```bash
./scripts/build.sh run
```

To upload (replace/upload target as you normally do with PlatformIO):

```bash
./scripts/build.sh run -t upload
```

Or run directly with Python:

```bash
python3 scripts/load_env_and_build.py run
```

PlatformIO build integration
----------------------------

This project injects secrets into the firmware at build time using PlatformIO's
`build_flags` (the project `platformio.ini` maps environment variables to
`-D` macros). Use the provided wrapper so your local `.env` values are visible
to PlatformIO during the build:

```bash
./scripts/build.sh run
```

When you build, the following environment variable names are used and will be
passed to the compiler as string macros: `WIFI_SSID`, `WIFI_PASS`, `APP_KEY`,
`APP_SECRET`, `DEVICE_ID_1`, `DEVICE_ID_2`, `DEVICE_ID_3`.

Do NOT commit your `.env` file. Keep secret values in your CI/CD platform's
secret store if you need builds in remote systems.

Make the repo git-push ready

The repository is already configured to ignore build artifacts and `.env`. To push this project to a remote Git repository, run:

```bash
git init
git add .
git commit -m "Initial project import: add dotenv support and gitignore"
# add your remote (example):
git remote add origin git@github.com:username/repo.git
git push -u origin main
```

Notes
- The build wrapper uses the `pio` or `platformio` CLI in your PATH. Install PlatformIO CLI if missing.
- The script intentionally does not overwrite environment variables already set in the shell.
- If your CI system needs environment variables, set them in the CI UI (do not commit `.env`).

If you'd like, I can also add a minimal `pre-commit` hook to prevent accidental commits of `.env` or add a GitHub Actions workflow template that uses repository secrets—tell me which you'd prefer.
