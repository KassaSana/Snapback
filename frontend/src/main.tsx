import React from "react";
import ReactDOM from "react-dom/client";

import { applyAppearance, readAppearanceMode } from "./appearance";
import App from "./App";
import "./styles.css";

applyAppearance(readAppearanceMode());

const root = document.getElementById("root");

if (!root) {
  throw new Error("Missing root element");
}

ReactDOM.createRoot(root).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>
);