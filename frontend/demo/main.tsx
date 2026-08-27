// Entry point for the hosted demo (`demo.html`).
//
// A separate entry, not a build flag inside `src/main.tsx`. The desktop bundle is built from
// `index.html` and never reaches this file, so no environment variable, dead-code elimination,
// or reviewer vigilance is what keeps sample data out of the shipped app — the module graph is.

import React from "react";
import ReactDOM from "react-dom/client";

import App from "../src/App";
import { applyAppearance, readAppearanceMode } from "../src/appearance";
import "../src/styles.css";

import DemoBanner from "./DemoBanner";
import { installDemoBridge } from "./bridge";

// Before React renders: App's effects call the bridge on mount, so it has to exist first.
installDemoBridge();

applyAppearance(readAppearanceMode());

const root = document.getElementById("root");

if (!root) {
  throw new Error("Missing root element");
}

ReactDOM.createRoot(root).render(
  <React.StrictMode>
    <DemoBanner />
    <App />
  </React.StrictMode>,
);
