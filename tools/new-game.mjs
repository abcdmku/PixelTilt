// Scaffold a new game from games/_template and re-run the registry generator.
//
//   npm run new-game -- space_dodge "SPACE DODGE"
//
// The id must be a valid C identifier (lowercase snake_case by convention) and
// becomes the directory name; the title (optional) is what the menu shows.
import { readFileSync, writeFileSync, mkdirSync, existsSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { generate } from "./gen-games.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const [id, titleArg] = process.argv.slice(2);

if (!id || !/^[a-z][a-z0-9_]*$/.test(id)) {
  console.error('usage: npm run new-game -- <id> ["TITLE"]');
  console.error("       id must match [a-z][a-z0-9_]*, e.g. space_dodge");
  process.exit(1);
}

const dir = join(root, "games", id);
if (existsSync(dir)) {
  console.error(`games/${id} already exists`);
  process.exit(1);
}

// Menu rows fit ~13 chars of 3x5 font.
const title = (titleArg ?? id.replace(/_/g, " ").toUpperCase()).slice(0, 13);

const template = readFileSync(join(root, "games", "_template", "game.cpp"), "utf8");
mkdirSync(dir, { recursive: true });
writeFileSync(join(dir, "game.cpp"), template.replaceAll("__ID__", id).replaceAll("__TITLE__", title));

generate();
console.log(`created games/${id}/game.cpp ("${title}")`);
console.log("next: edit it, then `npm run dev` to play it in the emulator");
