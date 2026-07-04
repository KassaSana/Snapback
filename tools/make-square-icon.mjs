import path from "node:path";
import { fileURLToPath } from "node:url";
import sharp from "sharp";

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), "..");
const src = path.join(root, "app-icon.png");
const out = path.join(root, "app-icon-square.png");
const size = 1024;

await sharp(src)
  .resize(size, size, { fit: "fill" })
  .png()
  .toFile(out);

console.log(out);
