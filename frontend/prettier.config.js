// Roadmap 4.12. Measured against the existing tree on 2026-08-19, not chosen:
// 2-space indent, double quotes (820 double-quoted strings in src/*.ts against 3 single),
// semicolons, trailing commas, and a 100-column width that 9,728 of 9,789 lines already fit.
//
// **Existing files were deliberately NOT reformatted** -- see CONTRIBUTING.md. Format what
// you touch. `npm run format:check` takes explicit paths for exactly that reason: run over
// the whole tree it would report the 61 long lines nobody intends to reflow today, and a
// check everyone learns to ignore is worse than no check.
export default {
  printWidth: 100,
  tabWidth: 2,
  semi: true,
  singleQuote: false,
  trailingComma: "all",
  bracketSpacing: true,
  arrowParens: "always",
  // The repo checks out CRLF on Windows (core.autocrlf) and LF elsewhere; git normalises on
  // commit. Pinning a value here would make the check fail on one platform or the other.
  endOfLine: "auto",
};
