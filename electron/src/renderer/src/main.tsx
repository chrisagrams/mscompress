import { StrictMode } from "react"
import { createRoot } from "react-dom/client"
import "./index.css"
import App from "./App.tsx"

// Dark mode default for the workbench.
if (!document.documentElement.classList.contains("light")) {
  document.documentElement.classList.add("dark")
}

// Tag the host platform so platform-aware chrome (e.g. the macOS toolbar inset
// past the traffic-light buttons) can be applied via CSS.
document.documentElement.classList.add(`platform-${window.api.platform}`)

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <App />
  </StrictMode>,
)
