# PlatformIO pre-build hook: refresh games/generated/game_list.cpp so newly
# added games are linked in without a manual `npm run gen`.
import shutil
import subprocess
import os

Import("env")  # noqa: F821 (provided by PlatformIO's SCons environment)

project_dir = env["PROJECT_DIR"]  # noqa: F821
node = shutil.which("node")
generated = os.path.join(project_dir, "games", "generated", "game_list.cpp")

if node:
    subprocess.run([node, os.path.join("tools", "gen-games.mjs")], cwd=project_dir, check=True)
elif not os.path.exists(generated):
    raise SystemExit(
        "games/generated/game_list.cpp is missing and node is not installed; "
        "run `npm run gen` once on a machine with Node.js"
    )
